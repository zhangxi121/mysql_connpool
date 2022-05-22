#include "threadpool.h"
#include "common.h"
#include <iostream>

#if defined(WIN32)
#include <windows.h>
#endif 

ThreadPool::ThreadPool(const size_t minNum, const size_t maxNum) : m_data(std::make_shared<data>())
{
    std::thread(&ThreadPool::ManagerTask, this).detach();

    for (size_t i = 0; i < minNum; ++i)
    {
        std::thread(&ThreadPool::WorkerTask, this).detach();
    }
    m_data->m_minNum = minNum;
    m_data->m_maxNum = maxNum;
    m_data->m_busyNum = 0;
    m_data->m_aliveNum = minNum;
    m_data->m_exitNum = 0;
    m_data->m_bShutdown = false;
}

ThreadPool::~ThreadPool()
{

    if ((bool)m_data)
    {
        {
            std::lock_guard<std::mutex> lk(m_data->m_mtx_pool);
            m_data->m_bShutdown = true;
        }
        m_data->m_cond_notEmpty.notify_all();

        // 阻塞回收管理者线程
        //.

        // 等待子线程都退出,
        while (0 != m_data->m_busyNum)
        {
            // 由于线程回调绑定了 this, 这里暂时先等待, 后续将线程处理函数与 this 剥离之后可以不用等待,
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 初始化时候 m_minNum 少就给予小的休眠时间, 初始化时候 m_minNum 大就给予大的休眠时间,
    }
}

void ThreadPool::ManagerTask()
{
    while (false == m_data->m_bShutdown)
    {
        // 每隔3s检测一次,添加线程和销毁线程,
        int dur = ThreadPool::POOL_CHECK_DURATION;
        std::this_thread::sleep_for(std::chrono::seconds(dur));
        // std::this_thread::sleep_for(std::chrono::seconds(ThreadPool::POOL_CHECK_DURATION));

        size_t taksNum = 0;
        int aliveNum = 0;
        int busyNum = 0;
        {
            std::lock_guard<std::mutex> lk(m_data->m_mtx_pool);
            taksNum = m_data->m_tasks.size();
            aliveNum = m_data->m_aliveNum;
        }
        {
            std::lock_guard<std::mutex> lk(m_data->m_mtx_busy);
            busyNum = m_data->m_busyNum;
        }

        // 添加线程
        // 当前任务个数 > 存活线程个数 && 存活线程个数 < 最大线程数,
        // pool->maxNum 不需要加锁是因为一旦初始化好了以后就不发生变化了,只是读这个值,因此不需要加锁,
        if (taksNum > aliveNum && aliveNum < m_data->m_maxNum)
        {
            std::lock_guard<std::mutex> lk(m_data->m_mtx_pool);
            int counter = 0;
            for (int i = 0; i < m_data->m_maxNum && counter < ThreadPool::INCREASE_DECREASE_NUMBER && m_data->m_aliveNum < m_data->m_maxNum; ++i)
            {
                LOG("======= increase thread  =======");
                std::thread(&ThreadPool::WorkerTask, this).detach();
                ++counter;
                ++m_data->m_aliveNum;
            }
        }

        // 销毁线程
        // 当忙的线程*2 < 存活的线程数  && 存活的线程>最小线程
        // pool->minNum 不需要加锁是因为一旦初始化好了以后就不发生变化了,只是读这个值,因此不需要加锁,
        if (busyNum * 2 < aliveNum && aliveNum > m_data->m_minNum)
        {
            // 让工作者线程自杀, 在 workerCb()中因为有条件变量阻塞
            m_data->m_exitNum = ThreadPool::INCREASE_DECREASE_NUMBER;
            for (int i = 0; i < INCREASE_DECREASE_NUMBER; i++)
            {
                LOG("======= decrease thread  =======");
                m_data->m_cond_notEmpty.notify_one();
            }
        }
    }
}

void ThreadPool::WorkerTask()
{
    for (;;)
    {
        m_data->m_mtx_pool.lock();
        while (m_data->m_tasks.size() == 0 && !m_data->m_bShutdown)
        {
            // 如果任务队列为空,就阻塞当前线程,
            bool bExit = false;
            try
            {
                std::unique_lock<std::mutex> lock(m_data->m_mtx_pool, std::adopt_lock);
                m_data->m_cond_notEmpty.wait(lock, [this, &bExit]()
                                             {
                                                 if (m_data->m_exitNum > 0)
                                                 {
                                                     --m_data->m_exitNum;
                                                     if (m_data->m_aliveNum > m_data->m_minNum)
                                                     {
                                                         --m_data->m_aliveNum;
                                                         bExit = true;
                                                         return bExit;
                                                     }
                                                 }
                                                 return bExit;
                                             });
            }
            catch (const std::exception &e)
            {
                m_data->m_mtx_pool.unlock();
                std::cerr << __FILE__ << __LINE__ << e.what() << '\n';
            }
            if (bExit)
            {
                LOG("======= one worker thread will be exit =======");
                m_data->m_mtx_pool.unlock();
                this->ThreadExit();
                LOG("======= one worker thread has exited =======");           
            }
        }
        // 判断线程池是否关闭了
        if (true == m_data->m_bShutdown)
        {
            m_data->m_mtx_pool.unlock();
            LOG("======= one worker thread will be exit =======");
            this->ThreadExit();
            LOG("======= one worker thread has exited =======");           
        }

        // 从任务队列中取出一个任务,
        auto currFunc = std::move(m_data->m_tasks.front());
        m_data->m_tasks.pop();
        m_data->m_mtx_pool.unlock();
        m_data->m_busyNum++;
        currFunc();
        m_data->m_busyNum--;
    }
}

void ThreadPool::ThreadExit()
{
#if defined(__unix__)
    LOG("threadExit() function: thread %lu exiting...", pthread_self());
    pthread_exit(NULL); // 主动退出,
#elif defined(WIN32)
    LogConsole::WinConsolePrint("======= win32 thread exit =======");
    ExitThread(0);
#endif 
}

int ThreadPool::GetBusyNum()
{
    int busyNum = 0;
    {
        std::lock_guard<std::mutex> lk(m_data->m_mtx_busy);
        busyNum = m_data->m_busyNum;
    }
    return busyNum;
}

int ThreadPool::GetAliveNum()
{
    int aliveNum = 0;
    {
        std::lock_guard<std::mutex> lk(m_data->m_mtx_pool);
        aliveNum = m_data->m_aliveNum;
    }
    return aliveNum;
}

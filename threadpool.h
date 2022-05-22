#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <iostream>


using namespace std;

class ThreadPool
{
public:
    using Task = std::function<void()>;

private:
    struct data
    {
        int m_minNum;
        int m_maxNum;
        std::atomic_int m_busyNum;
        std::atomic_int m_aliveNum;
        std::atomic_int m_exitNum;

        std::mutex m_mtx_pool;
        std::mutex m_mtx_busy;
        std::condition_variable m_cond_notEmpty;
        std::condition_variable m_cond_mgrBlk;
        bool m_bShutdown = false;
        std::queue<std::function<void()>> m_tasks;
    };

public:
    // 由于 std::mutex 不支持 拷贝 以及 赋值, 所以全部 delete,
    explicit ThreadPool(const size_t minNum, const size_t maxNum);
    ThreadPool() = delete;
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;
    ~ThreadPool();

public:
    void AddTask(const Task &task)
    {
        if (m_data->m_bShutdown == true)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(m_data->m_mtx_pool);
            m_data->m_tasks.emplace(task);
        }
        m_data->m_cond_notEmpty.notify_one();
        return;
    }

    void ManagerTask();

    void WorkerTask();

    void ThreadExit();

    int GetBusyNum();

    int GetAliveNum();

private:
    std::shared_ptr<data> m_data;
    static const int INCREASE_DECREASE_NUMBER = 2; //
    static const int POOL_CHECK_DURATION = 3;      // 每隔3s检测一次,添加线程和销毁线程,
};

#endif
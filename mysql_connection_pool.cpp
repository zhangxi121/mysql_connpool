
#include <iostream>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "mysql_connection_pool.h"

using namespace std;

namespace nmsp
{
    bool isAccess(const char *filepath)
    {
        if (access(filepath, F_OK) == 0)
            return true;
        else if (access(filepath, F_OK) == -1)
        {
            LOG("access() error: %s", strerror(errno));
        }
        return false;
    }

    std::string getImagePath(void)
    {
        char pidInfo[PATH_MAX] = {0};
        snprintf(pidInfo, PATH_MAX, "/proc/%d/exe", getpid());

        char exeFullPath[PATH_MAX + 1] = {0};
        readlink(pidInfo, exeFullPath, PATH_MAX);

        std::string imagePath = exeFullPath;
        return imagePath.substr(0, imagePath.rfind("/"));
    }

    std::string changePath(const char *path, const char *pattern)
    {
        std::string::size_type pos1, pos2;
        std::string s(path);
        std::string outputPath;
        outputPath.reserve(strlen(path));
        std::vector<std::string> vec_arr;

        pos2 = s.find(pattern);
        if (pos2 == 0)
        {
            outputPath += pattern;
        }
        pos1 = 0;
        while (std::string::npos != pos2)
        {
            std::string subStr = s.substr(pos1, pos2 - pos1);
            if (subStr == "..")
                vec_arr.pop_back();
            else
            {
                if (subStr != "")
                    vec_arr.emplace_back(subStr);
            }

            pos1 = pos2 + strlen(pattern);
            pos2 = s.find(pattern, pos1);
        }
        if (pos1 != s.length())
        {
            if (s.substr(pos1) != "..")
                vec_arr.emplace_back(s.substr(pos1));
        }

        size_t size = vec_arr.size();
        for (size_t i = 0; i < size; ++i)
        {
            if (i == size - 1)
            {
                outputPath = outputPath + vec_arr[i];
                break;
            }
            outputPath = outputPath + vec_arr[i] + pattern;
        }
        vec_arr.clear();

        return std::move(outputPath);
    }

    //-----------------------------------

    class MysqlConnection
    {
    public:
        MysqlConnection()
        {
            m_conn = mysql_init(nullptr);

            // 成功连接数据库后, 长时间不使用这个 conn 来进行操作,  在timeout值后, mysql-server 会关闭此连接,
            // 此时 mysql-client 并不知道, 客户端再次使用这个 conn 时候, 会有 "mysql server has gone away"  这样的错误, mysql_ping(),
        }

        ~MysqlConnection()
        {
            if (m_conn != nullptr)
            {
                mysql_close(m_conn);
                m_conn = nullptr;
            }
        }

    public:
        MYSQL *GetMysql() { return m_conn; }

        bool connect(string ip, unsigned short port, string user, string password, string dbname)
        {
            MYSQL *pmysql = mysql_real_connect(m_conn, ip.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0);
            return pmysql != nullptr;
        }

        bool update(string sql)
        {
            // 更新操作 insert、delete、update
            if (mysql_query(m_conn, sql.c_str()))
            {
                LOG("mysql_query err: %s", sql.c_str());
                return false;
            }
            return true;
        }

        void refreshAliveTime() { m_alivetime = time(NULL); }

        time_t getAliveTime() const { return time(NULL) - m_alivetime; }

        // 在这里不使用 return mysql_use_result(m_conn); 返回 返回 MYSQL_RES* 结果集, 造成结果集未释放,
        // 出现 "Commands out of sync; you can't run this command now", 这里给用户返回 MYSQL*, 让用户自己操作, 这样用户自己释放结果集,
        //

    private:
        MYSQL *m_conn;
        time_t m_alivetime;
    };

    //-----------------------------------

    ConnectionPool *ConnectionPool::m_instance = nullptr;

// #define DB_CONFIG_FILE "../config/db.json"
#define DB_CONFIG_FILE "mysqlConfig.ini"

    ConnectionPool::ConnectionPool()
    {
        std::string old_filepath = getImagePath() + "/" + (const char *)DB_CONFIG_FILE;
        std::string configfile = changePath(old_filepath.c_str());

        m_connectionCnt = 0;

        // 创建初始数量的连接
        if (!loadConfigFile(configfile.c_str()))
        {
            return;
        }

        // 创建初始数量的连接
        for (int i = 0; i < m_initSize; ++i)
        {
            MysqlConnection *p = new MysqlConnection();
            if (false == p->connect(m_ip, m_port, m_username, m_password, m_dbname))
            {
                LOG("mysql connect error!\n");
                continue;
            }
            p->refreshAliveTime(); // 刷新一下开始空闲的起始时间
            m_connectionQue.push(p);
            m_connectionCnt++;
        }

        thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
        produce.detach();

        thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
        scanner.detach();
    }

    ConnectionPool::~ConnectionPool()
    {
    }

    ConnectionPool *ConnectionPool::getConnectionPool()
    {
        static std::once_flag onceFlag;
        std::call_once(onceFlag, [&]
                       { m_instance = new ConnectionPool(); });
        return m_instance;
    }

    bool ConnectionPool::loadConfigFile(const char *configPath)
    {
        if (false == isAccess(configPath))
        {
            LOG("mysql config file is not exist! :%s", configPath);
            return false;
        }

        FILE *pf = fopen(configPath, "r");
        if (pf == nullptr)
        {
            LOG("fopen error");
            return false;
        }

        while (!feof(pf))
        {
            char line[1024] = {0};
            fgets(line, 1024, pf);
            string str = line;
            int idx = str.find('=', 0);
            if (idx == -1) // 无效的配置项
            {
                continue;
            }

            // password=123456\n
            int endidx = str.find('\n', idx);
            string key = str.substr(0, idx);
            string value = str.substr(idx + 1, endidx - idx - 1);

            if (key == "ip")
            {
                m_ip = value;
            }
            else if (key == "port")
            {
                m_port = atoi(value.c_str());
            }
            else if (key == "username")
            {
                m_username = value;
            }
            else if (key == "password")
            {
                m_password = value;
            }
            else if (key == "dbname")
            {
                m_dbname = value;
            }
            else if (key == "initSize")
            {
                m_initSize = atoi(value.c_str());
            }
            else if (key == "maxSize")
            {
                m_maxSize = atoi(value.c_str());
            }
            else if (key == "maxIdleTime")
            {
                m_maxIdleTime = atoi(value.c_str());
            }
            else if (key == "connectionTimeOut")
            {
                m_connectionTimeout = atoi(value.c_str());
            }
        }
        return true;
    }

    /*---
    // bool ConnectionPool::loadConfigFile(const char *configPath)
    // {
    //     // FILE *pf = fopen("/home/coder/MyWorkSpace/ConnPool/mysql_connection_pool/db.json", "r");

    //     if (false == isAccess(configPath))
    //     {
    //         TEST_LOG("mysql config file is not exist! :%s", configPath);
    //         return false;
    //     }

    //     cJSON *jsonRoot = NULL;
    //     cJSON *json_dbinfo = NULL, *json_access = NULL;
    //     cJSON *json_dbinfo_dbname = NULL, *json_dbinfo_server = NULL, *json_dbinfo_port = NULL;
    //     cJSON *json_access_username = NULL, *json_access_password = NULL;
    //     char jsonBuf[2048] = {0};

    //     mgrcomm::ReadFile readFile;
    //     readFile.InitCtx(configPath);
    //     const char *tmpBuf = readFile.GetContent();
    //     memcpy(jsonBuf, tmpBuf, strlen(tmpBuf));
    //     readFile.ReleaseCtx();

    //     jsonRoot = cJSON_Parse(jsonBuf);
    //     if (!jsonRoot)
    //     {
    //         TEST_LOG("json parse error : %s", configPath);
    //     }
    //     else
    //     {
    //         json_dbinfo = cJSON_GetObjectItem(jsonRoot, "dbinfo");
    //         if (json_dbinfo)
    //         {
    //             if (json_dbinfo->type == cJSON_Object)
    //             {
    //                 json_dbinfo_dbname = cJSON_GetObjectItem(json_dbinfo, "dbname");
    //                 if (json_dbinfo_dbname)
    //                     if (json_dbinfo_dbname->type == cJSON_String)
    //                         m_dbname = json_dbinfo_dbname->valuestring;

    //                 json_dbinfo_server = cJSON_GetObjectItem(json_dbinfo, "server");
    //                 if (json_dbinfo_server)
    //                     if (json_dbinfo_server->type == cJSON_String)
    //                         m_ip = json_dbinfo_server->valuestring;

    //                 json_dbinfo_port = cJSON_GetObjectItem(json_dbinfo, "port");
    //                 if (json_dbinfo_port)
    //                     if (json_dbinfo_port->type == cJSON_Number)
    //                         m_port = json_dbinfo_port->valueint;
    //             }
    //         }

    //         json_access = cJSON_GetObjectItem(jsonRoot, "access");
    //         if (json_access)
    //         {
    //             if (json_access->type == cJSON_Object)
    //             {
    //                 json_access_username = cJSON_GetObjectItem(json_access, "username");
    //                 if (json_access_username)
    //                     if (json_access_username->type == cJSON_String)
    //                         m_username = json_access_username->valuestring;

    //                 json_access_password = cJSON_GetObjectItem(json_access, "password");
    //                 if (json_access_password)
    //                     if (json_access_password->type == cJSON_String)
    //                         m_password = json_access_password->valuestring;
    //             }
    //         }
    //     }
    //     cJSON_Delete(jsonRoot);

    //     m_initSize = INIT_SIZE;
    //     m_maxSize = MAX_SIZE;
    //     m_maxIdleTime = MAX_IDLE_TIME;
    //     m_connectionTimeout = CONNECTION_TIMEOUT;

    //     return true;
    // }
    ---*/

    void ConnectionPool::produceConnectionTask()
    {
        for (;;)
        {
            unique_lock<mutex> lock(m_queueMutex);
            while (!m_connectionQue.empty())
            {
                m_produce_cv.wait(lock);
            }

            if (m_connectionCnt < m_maxSize)
            {
                MysqlConnection *p = new MysqlConnection();
                p->connect(m_ip, m_port, m_username, m_password, m_dbname);
                p->refreshAliveTime();
                m_connectionQue.push(p);
                m_connectionCnt++;
            }

            m_consumer_cv.notify_all();
        }
    }

    shared_ptr<MysqlConnection> ConnectionPool::getConnection()
    {
        unique_lock<mutex> lock(m_queueMutex);
        while (m_connectionQue.empty())
        {
            if (m_connectionCnt == 0)
            {
                m_produce_cv.notify_all();
            }

            // 查看连接状态,
            // MariaDB [(none)]> show full processlist;

            // m_consumer_cv.wait(lock);
            if (cv_status::timeout == m_consumer_cv.wait_for(lock, chrono::milliseconds(m_connectionTimeout)))
            {
                // 超时被唤醒,
                if (m_connectionQue.empty())
                {
                    LOG("获取空闲连接超时了...获取连接失败!");
                    return nullptr;
                }
            }
        }

        // 等待时间内被唤醒, 此时消费者拥有锁,
        // shared_ptr智能指针析构时，会把connection资源直接delete掉，相当于调用connection的析构函数，connection就被close掉了。
        // 这里需要自定义shared_ptr的释放资源的方式，把connection直接归还到queue当中
        shared_ptr<MysqlConnection> sp(m_connectionQue.front(),
                                       [&](MysqlConnection *pcon)
                                       {
                                        // 这里是在服务器应用线程中调用的，所以一定要考虑队列的线程安全操作
                                           unique_lock<mutex> lock(m_queueMutex);
                                           pcon->refreshAliveTime(); // 刷新一下开始空闲的起始时间
                                           m_connectionQue.push(pcon);
                                       });

        m_connectionQue.pop();
        m_produce_cv.notify_all(); // 消费完连接以后，通知生产者线程检查一下，如果队列为空了，赶紧生产连接
        return sp;
    }

    MYSQL *ConnectionPool::GetMysql()
    {
        return getConnection()->GetMysql();
    }

    bool ConnectionPool::Update(string sqlStr)
    {
        shared_ptr<MysqlConnection> connPtr = getConnection();
        return connPtr->update(sqlStr);
    }

    void ConnectionPool::scannerConnectionTask()
    {
        for (;;)
        {
            this_thread::sleep_for(chrono::seconds(m_maxIdleTime));

            // 扫描整个队列，释放多余的连接
            unique_lock<mutex> lock(m_queueMutex);
            while (m_connectionCnt > m_initSize)
            {
                MysqlConnection *p = m_connectionQue.front();
                if (p->getAliveTime() >= (time_t)m_maxIdleTime)
                {
                    m_connectionQue.pop();
                    m_connectionCnt--;
                    delete p;
                    LOG("m_connectionQue.size()=%lu", m_connectionQue.size());
                }
                else
                {
                    break;
                }
            }
        }
    }
}

//---------------------------------------------
// 当使用存储过程时候, 会有结果集未释放问题, 存储过程中结果集释放问题, 如下:

/***

    需要如下去释放结果集,
    (1).
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        //...
    }
    mysql_free_result(res);
    while (!mysql_next_result(mysql))
    {
        res = mysql_store_result(mysql);
        mysql_free_result(res);
    }

    (2).
    if (0 == (mysql_ret = mysql_real_query(mysql, callStr, strlen(callStr))))
    {
        do
        {
            MYSQL_RES *res = mysql_store_result(mysql);
            if (res != nullptr)
                mysql_free_result(res);
        } while (!mysql_next_result(mysql));
    }
    if (0 == (mysql_ret = mysql_query(mysql, selectStr))) // .. if ( 0 ==  mysql_real_query(mysql, selectStr, strlen(selectStr)))
    {
        MYSQL_RES *res = mysql_store_result(mysql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            if ((row = mysql_fetch_row(res)) != nullptr)
            {
                value = row[0];
            }
            mysql_free_result(res);

            while (!mysql_next_result(mysql))
            {
                res = mysql_store_result(mysql);
                mysql_free_result(res);
            }
        }
    }

namespace yuanshuo
{
    namespace dbrw
    {
        std::vector<StorageRepository> RepositoryDeviceModel::QueryStorageRepository()
        {
            std::vector<StorageRepository> vec_storageRepository;
            char sqlStr[] = "call proc_storage_get_all_repository()";

            MGR_VLOGINFO(ERR_CODE_TYPE::ERR_OK, "dbrw", "%s", sqlStr);

            yuanshuo::mgrcomm::ConnectionPool *cp = yuanshuo::mgrcomm::ConnectionPool::getConnectionPool();
            MYSQL *mysql = cp->GetMysql();

            int mysql_ret = 0;
            unsigned int sql_errno = 0;
            if (0 == (mysql_ret = mysql_real_query(mysql, sqlStr, strlen(sqlStr))))
            {
                MYSQL_RES *res = mysql_store_result(mysql);

                if (res != nullptr)
                {
                    std::map<std::string, unsigned int> map_field_idx;

                    // repositoryId, repositoryName, repositoryStatus, repositoryStatusReason, repositoryTypeId, repositoryTypeName, deviceCount, description, maxStreamCount
                    // '2', 'rp01', '1', '', '2', 'disk', '2', NULL, '200'

                    // unsigned long long record_rows = mysql_num_rows(res);
                    unsigned int record_columns = mysql_num_fields(res);
                    for (unsigned int i = 0; i < record_columns; ++i)
                    {
                        std::string fieldname = mysql_fetch_field_direct(res, i)->name;
                        map_field_idx[fieldname] = i;
                    }

                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(res)) != nullptr)
                    {
                        StorageRepository storageRepository;

                        uint64_t repositoryId = yuanshuo::mgrcomm::charToU64(row[map_field_idx["repositoryId"]]);
                        std::string repositoryName = row[map_field_idx["repositoryName"]];
                        int repositoryStatus = atoi(row[map_field_idx["repositoryStatus"]]);
                        std::string repositoryStatusReason = "";
                        if (nullptr != row[map_field_idx["repositoryStatusReason"]])
                        {
                            repositoryStatusReason = row[map_field_idx["repositoryStatusReason"]];
                        }
                        int repositoryTypeId = atoi(row[map_field_idx["repositoryTypeId"]]);
                        std::string repositoryTypeName = row[map_field_idx["repositoryTypeName"]];
                        int deviceCount = atoi(row[map_field_idx["deviceCount"]]);
                        std::string description = "";
                        if (nullptr != row[map_field_idx["description"]])
                        {
                            description = row[map_field_idx["description"]];
                        }
                        int maxStreamCount = atoi(row[map_field_idx["maxStreamCount"]]);

                        storageRepository.SetRepositoryId(repositoryId);
                        storageRepository.SetRepositoryName(repositoryName);
                        storageRepository.SetRepositoryStatus(repositoryStatus);
                        storageRepository.SetRepositoryStatusReason(repositoryStatusReason);
                        storageRepository.SetRepositoryTypeId(repositoryTypeId);
                        storageRepository.SetRepositoryTypeName(repositoryTypeName);
                        storageRepository.SetDeviceCount(deviceCount);
                        storageRepository.SetDescription(description);
                        storageRepository.SetMaxStreamCount(maxStreamCount);

                        vec_storageRepository.emplace_back(storageRepository);
                    }
                    mysql_free_result(res);

                    while (!mysql_next_result(mysql))
                    {
                        res = mysql_store_result(mysql);
                        mysql_free_result(res);
                    }
                }
            }
            else
            {
                sql_errno = mysql_errno(mysql);
                MGR_VLOGERROR(ERR_CODE_TYPE::ERR_OK, "dbrw", "sql_errno = %d, mysql_real_query(%s)", sql_errno, sqlStr);
            }

            return vec_storageRepository;
        }

        //
        //

        MgrNodeProperty MgrNodePropertyModel::QueryProperty(const MgrNodeBriefProperty &mgrNodeBriefProperty)
        {
            int id = mgrNodeBriefProperty.GetId();

            char callStr[256] = {};
            sprintf(callStr, "call proc_node_client_get_all_properties(%d, @result)", id);

            MGR_VLOGINFO(ERR_CODE_TYPE::ERR_OK, "dbrw", "%s", callStr);

            char selectStr[] = "select @result";
            std::string value;

            // SET @test='{}';
            // CALL `proc_node_client_get_all_properties`(id, @test);
            // SELECT @test;

            yuanshuo::mgrcomm::ConnectionPool *cp = yuanshuo::mgrcomm::ConnectionPool::getConnectionPool();
            MYSQL *mysql = cp->GetMysql();

            int mysql_ret = 0;
            unsigned int sql_errno = 0;
            if (0 == (mysql_ret = mysql_real_query(mysql, callStr, strlen(callStr))))
            {
                do
                {
                    MYSQL_RES *res = mysql_store_result(mysql);
                    if (res != nullptr)
                        mysql_free_result(res);
                } while (!mysql_next_result(mysql));
            }
            else
            {
                sql_errno = mysql_errno(mysql);
                MGR_VLOGERROR(ERR_CODE_TYPE::ERR_OK, "dbrw", "sql_errno = %d, mysql_real_query(%s)", sql_errno, callStr);
            }

            if (0 == (mysql_ret = mysql_query(mysql, selectStr))) // .. if ( 0 ==  mysql_real_query(mysql, selectStr, strlen(selectStr)))
            {
                MYSQL_RES *res = mysql_store_result(mysql);
                if (res != nullptr)
                {
                    MYSQL_ROW row;
                    if ((row = mysql_fetch_row(res)) != nullptr)
                    {
                        value = row[0];
                    }
                    mysql_free_result(res);

                    while (!mysql_next_result(mysql))
                    {
                        res = mysql_store_result(mysql);
                        mysql_free_result(res);
                    }
                }
            }
            else
            {
                sql_errno = mysql_errno(mysql);
                MGR_VLOGERROR(ERR_CODE_TYPE::ERR_OK, "dbrw", "sql_errno = %d, mysql_query(%s)", sql_errno, selectStr);
            }

            MgrNodeProperty mgrNodeProperty = ParseJsonProperty(value.c_str());
            (MgrNodeBriefProperty &)mgrNodeProperty = mgrNodeBriefProperty;
            return mgrNodeProperty;
        }

    }
}
***/

//---------------------------------------------

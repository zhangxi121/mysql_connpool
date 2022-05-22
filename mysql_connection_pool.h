#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <queue>
#include <string>
#include <thread>



namespace nmsp
{
    bool isAccess(const char *filepath);
    std::string getImagePath(void);
    std::string changePath(const char *path, const char *pattern = "/");

    class MysqlConnection;

    //------------------

    class ConnectionPool
    {
    public:
        ~ConnectionPool();

    public:
        static ConnectionPool *getConnectionPool();

        std::shared_ptr<MysqlConnection> getConnection();

        MYSQL *GetMysql();

        bool Update(std::string sqlStr);

    private:
        ConnectionPool();
        ConnectionPool(const ConnectionPool &that) = delete;

    private:
        bool loadConfigFile(const char *configPath);

        void produceConnectionTask();

        void scannerConnectionTask();

    private:
        std::string m_ip;
        unsigned short m_port;
        std::string m_username;
        std::string m_password;
        std::string m_dbname;
        int m_initSize;
        int m_maxSize;
        int m_maxIdleTime;
        int m_connectionTimeout;

        std::queue<MysqlConnection *> m_connectionQue;
        std::mutex m_queueMutex;
        std::atomic_int m_connectionCnt;
        std::condition_variable m_produce_cv;
        std::condition_variable m_consumer_cv;

    private:
        static ConnectionPool *m_instance;
    };
}

#endif

#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <mysql/mysql.h>
#include <string.h>
#include <thread>
#include <vector>



#include "mysql_connection_pool.h"
#include "common.h"
#include "threadpool.h"

using namespace std;
using namespace nmsp;

//---------------------

class Connection
{
public:
	Connection()
	{
		m_conn = mysql_init(nullptr);
	}

	~Connection()
	{
		if (m_conn != nullptr)
		{
			mysql_close(m_conn);
			m_conn = nullptr;
			// printf("mysql conn close ...\n");
		}
	}

public:
	MYSQL *GetMysql() { return m_conn; }

	bool connect(string ip, unsigned short port, string user, string password, string dbname)
	{
		MYSQL *p = mysql_real_connect(m_conn, ip.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, CLIENT_MULTI_STATEMENTS);
		return p != nullptr;
	}

	bool update(string sql)
	{
		// 更新操作 insert、delete、update
		if (mysql_query(m_conn, sql.c_str()))
		{
			LOG("更新失败: %s", sql.c_str());
			return false;
		}
		return true;
	}

private:
	MYSQL *m_conn;
};

//---------------------

/*

连接池:
	mysql> create database chat01;
	mysql> use chat01;

	mysql> CREATE TABLE user (
				id int(11) PRIMARY KEY  AUTO_INCREMENT,
				name varchar(50),
				age int(11),
				sex enum('male','female')
			)
	Query OK, 0 rows affected (0.02 sec)

	mysql> desc user;
		+-------+-----------------------+------+-----+---------+----------------+
		| Field | Type                  | Null | Key | Default | Extra          |
		+-------+-----------------------+------+-----+---------+----------------+
		| id    | int(11)               | NO   | PRI | NULL    | auto_increment |
		| name  | varchar(50)           | YES  |     | NULL    |                |
		| age   | int(11)               | YES  |     | NULL    |                |
		| sex   | enum('male','female') | YES  |     | NULL    |                |
		+-------+-----------------------+------+-----+---------+----------------+


	[coder@centos my_common_connection_pool]$ strace ./mysql_test 2  >& ./mystrace.txt
	[coder@centos my_common_connection_pool]$ strace ./mysql_test 2  >& ./mystrace_localhost.txt
	[coder@centos my_common_connection_pool]$ vim mystrace.txt
		#
		# mysql_real_connect（"127.0.0.1", xxx, ...) 调用 connect(12, {sa_family=AF_INET, sin_port=htons(3306), sin_addr=inet_addr("127.0.0.1")}, 16) = -1 EINPROGRESS (Operation now in progress)
		# mysql_real_connect（"localhost", xxx, ...) 调用 connect(10, {sa_family=AF_UNIX, sun_path="/var/lib/mysql/mysql.sock"}, 110) = 0
		# 所以只需要 mysql_real_connect("localhost", xxx, ...) 就可以了, 调用的就是 connect(10, {sa_family=AF_UNIX, sun_path="/var/lib/mysql/mysql.sock"}, 110) = 0 ,
		#

	压力测试:
		ip = localhost
		数据量           未使用连接池花费时间            	 使用连接池花费时间
		1000条           单线程 40ms   四线程 60ms           单线程 30ms   四线程 20ms
		5000条           单线程 260ms  四线程 280ms          单线程 150ms  四线程 110ms
		10000条          单线程 510ms  四线程 580ms          单线程 320ms  四线程 220ms

		ip = 127.0.0.1
		数据量           未使用连接池花费时间            	 使用连接池花费时间
		1000条           单线程 80ms   四线程                单线程   四线程
		5000条           单线程 420ms  四线程                单线程   四线程
		10000条          单线程 840ms  四线程                单线程   四线程


	MariaDB [chat01]> delete from user;
		Query OK, 1000 rows affected (0.004 sec)


*/
#define SUM_COUNT 1000
// #define SUM_COUNT 5000
// #define SUM_COUNT 10000

// #define PER_THREAD_COUNT 250
#define PER_THREAD_COUNT 1250
// #define PER_THREAD_COUNT 2500

int main001()
{
	/*
	Connection conn;
	char sqlStr[1024] = {};
	sprintf(sqlStr,  "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
	// conn.connect("10.4.7.9", 3306, "root", "123456", "chat01");
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat01");
	conn.update(sqlStr);
	*/

#if 0
// #if 1
	// 单线程测试:
	clock_t begin = clock();
	ConnectionPool *cp = ConnectionPool::getConnectionPool();
	for (int i = 0; i < SUM_COUNT; ++i)
	{

		Connection conn;
		char sqlStr[1024] = {};
		sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
		// conn.connect("localhost", 3306, "root", "123456", "chat01");
		conn.connect("127.0.0.1", 3306, "root", "123456", "chat01");
		conn.update(sqlStr);

		/*	
		shared_ptr<Connection>  sp =  cp->getConnection();
		char sqlStr[1024] = {};
		sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
		sp->update(sqlStr);		
		*/
	}
	clock_t end = clock();
	cout << (end - begin) * 1000 / CLOCKS_PER_SEC << " ms" << endl;

#else
	// #elif 0
	Connection conn;
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat01");

	auto no_pool_thread_task = []()
	{
		for (int i = 0; i < PER_THREAD_COUNT; ++i)
		{
			Connection conn;
			conn.connect("localhost", 3306, "root", "123456", "chat01");
			char sqlStr[1024] = {};
			sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
			conn.update(sqlStr);
		}
	};

	(void)no_pool_thread_task;

	auto connpool_thread_task = []()
	{
		ConnectionPool *cp = ConnectionPool::getConnectionPool();
		for (int i = 0; i < PER_THREAD_COUNT; ++i)
		{
			char sqlStr[1024] = {};
			sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
			cp->Update(sqlStr);
		}
	};

	// 多线程测试:
	clock_t begin = clock();

	// thread t1(no_pool_thread_task);
	// thread t2(no_pool_thread_task);
	// thread t3(no_pool_thread_task);
	// thread t4(no_pool_thread_task);

	thread t1(connpool_thread_task);
	thread t2(connpool_thread_task);
	thread t3(connpool_thread_task);
	thread t4(connpool_thread_task);

	t1.join();
	t2.join();
	t3.join();
	t4.join();

	clock_t end = clock();
	cout << (end - begin) * 1000 / CLOCKS_PER_SEC << " ms" << endl;

#endif

	return 0;
}

/////////////////////////////////////////////
/////////////////////////////////////////////

int main002(int argc, const char *argv[])
{
	if (argc != 3)
	{
		cout << "input error, please input <execute> <task_num> <thread_num>" << endl;
		return -1;
	}
	int max_task_num = atoi(argv[1]);
	int thread_num = atoi(argv[2]);
	int thread_loop_num = max_task_num / thread_num;

	vector<std::thread *> vec_thread;

	auto thread_task = [=]()
	{
		ConnectionPool *cp = ConnectionPool::getConnectionPool();
		for (int i = 0; i < thread_loop_num; ++i)
		{
			char sqlStr[1024] = {};
			sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
			cp->Update(sqlStr);
		}
	};

	// 在 ConnectionPool::ConnectionPool() 做初始化,连接数据库,
	// 这里提前做了一次连接是因为我获取到链接之后, 插入的 10000条 数据是一样的, 有时候可能会有问题, 小概率事件, 加入这么一条之后就不会出现,
	// 如果插入的数据不完全都是一样的,可以不用在这里提前做一次连接, 直接会在 ConnectionPool::ConnectionPool()  完成连接,
	Connection conn;
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat01");

	// 多线程测试:
	clock_t begin = clock();

	for (int i = 0; i < thread_num; ++i)
	{
		vec_thread.emplace_back(new thread(thread_task));
	}
	size_t size = vec_thread.size();
	for (size_t i = 0; i < size; ++i)
	{
		vec_thread[i]->join();
	}

	clock_t end = clock();
	cout << (end - begin) * 1000 / CLOCKS_PER_SEC << " ms" << endl;

	for (size_t i = 0; i < size; ++i)
	{
		delete vec_thread[i];
		vec_thread[i] = nullptr;
	}
	vec_thread.clear();

	return 0;
}

//===================================================

/// connectpool  + threadpool  ...

int main(int argc, const char *argv[])
{
	if (argc != 4)
	{
		cout << "input error, please input <execute> <task_num> <min_thread_num> <max_thread_num>" << endl;
		return -1;
	}
	int task_num = atoi(argv[1]);
	int min_thread_num = atoi(argv[2]);
	int max_thread_num = atoi(argv[3]);

	// 在 ConnectionPool::ConnectionPool() 做初始化,连接数据库,
	// 这里提前做了一次连接是因为我获取到链接之后, 插入的 10000条 数据是一样的, 有时候可能会有问题, 小概率事件, 加入这么一条之后就不会出现,
	// 如果插入的数据不完全都是一样的,可以不用在这里提前做一次连接, 直接会在 ConnectionPool::ConnectionPool()  完成连接,
	Connection conn;
	conn.connect("127.0.0.1", 3306, "root", "123456", "chat01");

	auto thread_task = [=]()
	{
		ConnectionPool *cp = ConnectionPool::getConnectionPool();
		char sqlStr[1024] = {};
		sprintf(sqlStr, "insert into user(name,age,sex) values('%s',%d,'%s')", "zhangsan", 20, "male");
		shared_ptr<MysqlConnection> sp = cp->getConnection();
		cp->Update(sqlStr);
		std::cout << sqlStr << std::endl;
	};

	ThreadPool pool(min_thread_num, max_thread_num); // 最大10个线程,
	for (int i = 0; i < task_num; ++i)
	{
		pool.AddTask(thread_task);
	}

	this_thread::sleep_for(chrono::seconds(20));

	return 0;
}
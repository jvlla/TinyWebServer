#include <queue>
#include <mutex>
#include <condition_variable>
#include <mysql/mysql.h>
#include <assert.h>
#include <iostream>
#include <signal.h>
#include "SQLConnPool.h"
#include "../Log/Log.h"
using namespace std;

queue<MYSQL *> SQLConnPool::conn_queue_;
mutex SQLConnPool::mutex_;
condition_variable SQLConnPool::cond_;
string SQLConnPool::host_;
int SQLConnPool::port_;
string SQLConnPool::user_;
string SQLConnPool::password_;
string SQLConnPool::database_name_;

SQLConnPool::SQLConnPool()
{
    cout << "connection pool created" << endl;
}

SQLConnPool::~SQLConnPool()
{
    unique_lock<mutex> locker(mutex_);
    while (!conn_queue_.empty())
    {
        MYSQL * conn = conn_queue_.front();
        conn_queue_.pop();
        mysql_close(conn);
    }
    mysql_library_end();
    cout << "connection pool destroyed" << endl;
}

void SQLConnPool::init(const string host, int port, const string user,const string password, const string database_name, int conn_limit)
{
    unique_lock<mutex> locker(mutex_);

    host_ = host;
    user_ = user;
    port_ = port;
    password_ = password;
    database_name_ = database_name;
    assert(conn_limit > 0);

    for (int i = 0; i < conn_limit; ++i) {
        MYSQL * conn = nullptr;

        conn = mysql_init(conn);
        if (!conn)
        {
            cerr << "mysql init failed" << endl;
            raise(SIGINT);
        }
        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), password_.c_str(), database_name_.c_str(), port_, nullptr, 0))
        {
            cerr << "connect to mysql failed" << endl;
            raise(SIGINT);
        }
        
        conn_queue_.push(conn);
    }
}

MYSQL * SQLConnPool::get_conn()
{
    unique_lock<mutex> locker(mutex_);
    cond_.wait(locker, [](){ return !conn_queue_.empty();});
    MYSQL * conn = conn_queue_.front();
    conn_queue_.pop();
    locker.unlock();

    /* 由于长时间未使用或数据库重启等原因，再次使用可能需要数据库重连。且设置MYSQL_OPT_RECONNECT好像不起作用，暂时只能自己重连 */
    if (mysql_ping(conn) != 0)
    {
        log(Log::WARN, "mysql disconnected");
        conn = mysql_init(conn);
        if (!conn)
        {
            log(Log::FATAL, "mysql reinit failed");
            raise(SIGINT);
        }
        if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), password_.c_str(), database_name_.c_str(), port_, nullptr, 0))
        {
            log(Log::FATAL, "mysql reconnect failed");
            raise(SIGINT);
        }
        else
            log(Log::INFO, "mysql reconnect success");
    }

    return conn;
}

void SQLConnPool::free_conn(MYSQL * conn)
{
    std::unique_lock<std::mutex> locker(mutex_);
    conn_queue_.push(conn);
    cond_.notify_one();
    locker.unlock();
}

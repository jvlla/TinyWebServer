#include <queue>
#include <mutex>
#include <condition_variable>
#include <mysql/mysql.h>
#include <assert.h>
#include <stdio.h>
#include "SQLConnPool.h"
using namespace std;

std::queue<MYSQL *> SQLConnPool::conn_queue_;
std::mutex SQLConnPool::mutex_;
std::condition_variable SQLConnPool::cond_;

SQLConnPool::SQLConnPool()
{
    printf("thread pool created\n");
}

SQLConnPool::~SQLConnPool()
{
    std::unique_lock<std::mutex> locker(mutex_);
    while (!conn_queue_.empty())
    {
        MYSQL * conn = conn_queue_.front();
        conn_queue_.pop();
        mysql_close(conn);
    }
    mysql_library_end();
    printf("thread pool destroyed\n");
}

void SQLConnPool::init(const char * host, int port, const char * user,const char * password, const char * database_name, int conn_limit)
{
    std::unique_lock<std::mutex> locker(mutex_);
    assert(conn_limit > 0);
    for (int i = 0; i < conn_limit; ++i) {
        MYSQL * conn = nullptr;
        conn = mysql_init(conn);
        if (!conn)
        {
            fprintf(stderr, "mysql init failed\n");
            exit(-1);
        }
        if (!mysql_real_connect(conn, host, user, password, database_name, port, nullptr, 0))
        {
            fprintf(stderr, "mysql connect failed\n");
            exit(-1);
        }
        conn_queue_.push(conn);
    }
}

SQLConnPool& SQLConnPool::get_instance() 
{
    static SQLConnPool instance;
    return instance;
}

MYSQL * SQLConnPool::get_conn()
{
    std::unique_lock<std::mutex> locker(mutex_);
    cond_.wait(locker, [](){ return !conn_queue_.empty();});
    MYSQL * conn = conn_queue_.front();
    conn_queue_.pop();

    return conn;
}

bool SQLConnPool::free_conn(MYSQL * conn)
{
    std::unique_lock<std::mutex> locker(mutex_);
    conn_queue_.push(conn);
    cond_.notify_one();
    locker.unlock();
    return true;
}

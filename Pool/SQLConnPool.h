#ifndef __SQL_CONN_POOL__
#define __SQL_CONN_POOL__

#include <queue>
#include <mutex>
#include <condition_variable>
#include <mysql/mysql.h>
#include <assert.h>
#include <stdio.h>

class SQLConnPool {
public:
    SQLConnPool(const SQLConnPool&) = delete;
    SQLConnPool & operator=(const SQLConnPool&) = delete;
    ~SQLConnPool();
    void init(const char * host, int port, const char * user,const char * password, const char * database_name, int conn_limit = 20);
    static SQLConnPool& get_instance();
    MYSQL * get_conn();
    bool free_conn(MYSQL * conn);

private:
    SQLConnPool();

    static std::queue<MYSQL *> conn_queue_; // 任务队列
    static std::mutex mutex_;               // 用于互斥访问的锁 
    static std::condition_variable cond_;   // 用于互斥访问的条件变量
};

#endif
#ifndef __SQL_CONN_POOL__
#define __SQL_CONN_POOL__

#include <queue>
#include <mutex>
#include <condition_variable>
#include <mysql/mysql.h>
#include <assert.h>
#include <stdio.h>
#include "../Singleton.h"

class SQLConnPool: public Singleton<SQLConnPool> {
friend Singleton<SQLConnPool>;

public:
    void init(const std::string host, int port, const std::string user, const std::string password, const std::string database_name, int conn_limit = 20);
    MYSQL * get_conn();
    bool free_conn(MYSQL * conn);

protected:
    SQLConnPool();
    virtual ~SQLConnPool();

private:
    static std::queue<MYSQL *> conn_queue_; // 任务队列
    static std::mutex mutex_;               // 用于互斥访问的锁 
    static std::condition_variable cond_;   // 用于互斥访问的条件变量
    static std::string host_;
    static int port_;
    static std::string user_;
    static std::string password_;
    static std::string database_name_;
};

#endif
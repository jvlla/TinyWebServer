#ifndef __SQL_CONN_POOL__
#define __SQL_CONN_POOL__

#include <queue>
#include <mutex>
#include <condition_variable>
#include <mysql/mysql.h>
#include <assert.h>
#include "../Singleton.h"

class SQLConnPool: public Singleton<SQLConnPool> {
friend Singleton<SQLConnPool>;

public:
    /* 初始化函数，参数分别为MYSQL主机IP、MYSQL端口、MYSQL登录用户名、MYSQL登录密码、MYSQL使用数据库名和默认初始连接数 */
    void init(const std::string host, int port, const std::string user, const std::string password
        , const std::string database_name, int conn_limit = 100);
    /* 获取连接 */
    MYSQL * get_conn();
    /* 释放连接 */
    void free_conn(MYSQL * conn);

protected:
    SQLConnPool();
    virtual ~SQLConnPool();

private:
    static std::queue<MYSQL *> conn_queue_; // 任务队列
    static std::mutex mutex_;               // 用于互斥访问的锁 
    static std::condition_variable cond_;   // 用于互斥访问的条件变量
    static std::string host_;               // MySQL主机IP
    static int port_;                       // MYSQL端口
    static std::string user_;               // MYSQL登录用户名
    static std::string password_;           // MYSQL登录密码
    static std::string database_name_;      // MYSQL使用数据库名
};

#endif
#ifndef __LOG_H__
#define __LOG_H__
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <unistd.h>
#include <libgen.h>
#include "../Singleton.h"

/* 通过宏自动替换文件名、行号 */
#define log(level, msg) \
    Log::get_instance().write(gettid(), level, basename((char *) __FILE__), __LINE__, msg);

class Log: public Singleton<Log> {
friend Singleton<Log>;  // 设置友元解决Singleton类访问权限问题

public:
    /* 日志级别 */
    enum LOG_LEVEL {DEBUG, INFO, WARN, ERROR, FATAL};
    /* 日志初始化，参数为进程id、日志文件夹、日志名、默认日志级别（一下级别不写出）和异步缓冲队列大小 */
    void init(pid_t process_id, std::string dir_path, std::string log_name, enum LOG_LEVEL level, long queue_size = 1024);
    /* 写入日志，参数为日志级别、代码文件名、代码行号和日志信息 */
    void write(pid_t thread_id, enum LOG_LEVEL level, std::string file_name, int line_num, std::string msg);

protected:
    Log();
    virtual ~Log();

private:
    /* 清空日志缓冲队列，写出至日志文件 */
    void flush();

    std::mutex mutex_;                      // 用于互斥访问的锁
    std::condition_variable empty_, full_;  // 用于互斥访问的条件变量
    bool stop_flush_;                       // 是否停止清空队列线程
    unsigned long long day_count_;          // 自1970以来共有多少天，用于打开新日志
    std::queue<std::string> log_queue_;         // 日志队列
    unsigned long queue_capacity_;          // 日志队列最大容量
    enum LOG_LEVEL defalut_level_;          // 日志默认级别，以下忽略
    pid_t process_id_;
    std::string dir_path_, log_name_;       // 默认日志文件夹和日志名
    std::fstream stream_;                   // 日志文件流
};

#endif
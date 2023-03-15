#ifndef __LOG_H__
#define __LOG_H__
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <stdio.h>
#include "../Singleton.h"

/* 通过宏自动替换文件名、行号 */
#define log(level, msg) \
{\
    std::size_t pos = std::string(__FILE__).find_last_of('/');\
    if (pos != std::string::npos)\
        Log::get_instance().write(level, std::string(__FILE__).substr(pos + 1), __LINE__, msg);\
    else\
        Log::get_instance().write(level, __FILE__, __LINE__, msg);\
}

class Log: public Singleton<Log> {
friend Singleton<Log>;  // 直接设友元解决访问权限靠谱吗？

public:
    enum LOG_LEVEL {DEBUG, INFO, WARN, ERROR, FATAL};
    void init(pid_t process_id, std::string dir_path, std::string log_name, enum LOG_LEVEL level, long queue_size = 1024);
    void write(enum LOG_LEVEL level, std::string file_name, int line_num, std::string msg);

protected:
    Log();
    virtual ~Log();

private:
    void flush();
    /* 停止写出线程，如果日志文件已打开，写出全部日志内容并关闭文件 */
    void stop_log();

    std::mutex mutex_;                      // 用于互斥访问的锁
    std::condition_variable empty_, full_;  // 用于互斥访问的条件变量
    bool stop_flush_;                       // 是否停止清空队列线程
    unsigned long long day_count_;           // 自1970以来共有多少天，用于打开新日志
    std::queue<std::string> queue_;         // 日志队列
    unsigned long queue_capacity_;                    // 日志队列最大容量
    enum LOG_LEVEL defalut_level_;          // 日志默认级别，以下忽略
    pid_t process_id_;
    std::string dir_path_, log_name_;       // 默认日志文件夹和日志名
    std::fstream stream_;                  // 日志文件流
};

#endif
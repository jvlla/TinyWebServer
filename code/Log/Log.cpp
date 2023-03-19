#include <thread>
#include <iostream>
#include <signal.h>
#include <limits.h>
#include <stdio.h>
#include "Log.h"
#define FORMAT_SIZE 64  // 用于格式化字符串缓存的大小
using namespace std;

bool open_log_file(fstream &stream, string dir_path, string log_name, pid_t process_id);
unsigned long long get_day_count();

Log::Log()
{
    queue_capacity_ = INT_MAX;
    std::thread thread([this](){
        while (!stop_flush_)
        {
            std::unique_lock<std::mutex> locker(mutex_);
            full_.wait(locker, [this](){ return log_queue_.size() >= queue_capacity_ || stop_flush_;});
            if (stop_flush_)
                break;
            flush();
            empty_.notify_all();  // 通知所有写线程
            locker.unlock();
        }
        cout << "log thread out" << endl;
    });
    thread.detach();
    cout << "log created" << endl;
}

Log::~Log()
{
    unique_lock<mutex> locker(mutex_);
    if (stream_.is_open())
    {
        flush();
        stream_.close();
    }
    stop_flush_ = true;
    full_.notify_all();
    locker.unlock();
    this_thread::sleep_for(std::chrono::milliseconds(500));  // 等待线程退出
    cout << "log destroied" << endl;
}

void Log::init(pid_t process_id, std::string dir_path, std::string log_name, enum LOG_LEVEL level, long queue_capacity)
{
    unique_lock<mutex> locker(mutex_);
    stop_flush_ = false;
    day_count_ = get_day_count();
    queue_capacity_ = queue_capacity > 1 ? queue_capacity : 1;  // 日志队列大小至少为1
    defalut_level_ = level;
    process_id_ = process_id;
    dir_path_ = dir_path;
    log_name_ = log_name;
    if (!open_log_file(stream_, dir_path_, log_name_, process_id_))
    {
        locker.unlock();
        raise(SIGINT);
    }
}

void Log::write(pid_t thread_id, enum LOG_LEVEL level, std::string file_name, int line_num, std::string msg)
{
    unique_lock<mutex> locker(mutex_);
    while (log_queue_.size() >= queue_capacity_)
        empty_.wait(locker);
    // 低于默认日志级别，不写入
    if (static_cast<int>(level) < static_cast<int>(defalut_level_))
        return;
    string log;
    char temp[FORMAT_SIZE];

    /* 生成格式化时间 */
    time_t now = time(nullptr);
    struct tm * local_now = localtime(&now);
    snprintf(temp, FORMAT_SIZE - 1, "%d%02d%02d %02d:%02d:%02d ", local_now->tm_year + 1900, local_now->tm_mon + 1, local_now->tm_mday,
        local_now->tm_hour, local_now->tm_min, local_now->tm_sec);
    log += temp;

    /* 生成写日志线程号 */
    snprintf(temp, FORMAT_SIZE - 1, "%6d ", thread_id);
    log += temp;

    /* 生成日志级别 */
    switch (level)
    {
        case DEBUG:
            log += "DEBUG";
            break;
        case INFO:
            log += "INFO ";
            break;
        case WARN:
            log += "WARN ";
            break;
        case ERROR:
            log += "ERROR";
            break;
        case FATAL:
            log += "FATAL";
            break;
    };
    log += " ";

    /* 生成日志文件名、行号和消息 */
    snprintf(temp, FORMAT_SIZE - 1, "%26s: %4d  ", file_name.c_str(), line_num);
    log += temp;
    log += msg;

    /* 如果处于新的一天，写出日志并打开新的日志文件 */
    if (day_count_ != get_day_count())
    {
        day_count_ = get_day_count();  // 在发现新一天后立即更新，避免释放锁后其它写线程获得，产生多个同一天的日志文件
        full_.notify_one();
        while (log_queue_.size() >= queue_capacity_)
            empty_.wait(locker);
        if (stream_.is_open())
            stream_.close();
        if (!open_log_file(stream_, dir_path_, log_name_, process_id_))
        {
            locker.unlock();
            raise(SIGINT);
        }
    }
    /* 新日志入队，如果队列已满，通过信号量通知线程写出 */
    log_queue_.push(log);
    if (log_queue_.size() >= queue_capacity_)
        full_.notify_one();
}

void Log::flush()
{
    // 不需要加锁，flush()函数在队列写满、日期变更和析构时调用，已有持有锁
    while (!log_queue_.empty())
    {
        stream_ << log_queue_.front() << endl;
        log_queue_.pop();
    }
}

bool open_log_file(fstream &stream, string dir_path, string log_name, pid_t process_id)
{
    string file_name = dir_path + "/logfile_" + log_name + ".";
    char temp[FORMAT_SIZE];
    time_t now = time(nullptr);
    struct tm * local_now = localtime(&now);
    snprintf(temp, FORMAT_SIZE - 1, "%d%02d%02d", local_now->tm_year + 1900, local_now->tm_mon + 1, local_now->tm_mday);
    file_name += temp;
    file_name += "." + to_string(process_id) + ".log";

    stream.open(file_name, ios::out | ios::trunc);

    if (stream.is_open())
    {
        stream << "日期     时间       线程 级别                      文件名: 行号  正文" << endl;  // 输出字段说明
        return true;
    }
    else
    {
        /* 如果出错，返回false，由其它函数决定是否终止运行 */
        cerr << "can't open log file " << file_name << endl;
        return false;
    }
}

unsigned long long get_day_count()
{
    return  time(nullptr) / (60 * 60 * 24);
}

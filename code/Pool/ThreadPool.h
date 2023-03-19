#ifndef __THREAD_POOL__
#define __THREAD_POOL__

#include <queue>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <unistd.h>
#include <stdio.h>
#include "../Singleton.h"

class ThreadPool: public Singleton<ThreadPool> {
friend Singleton<ThreadPool>;

public:
    bool add_task(std::function<void()> func)
    {
        std::unique_lock<std::mutex> locker(mutex_);
        if (tasks_count_ < MAX_TASKS_)
        {
            task_queue_.push(func);
            ++tasks_count_;
            cond_.notify_one();
            return true;
        }
        else
            return false;
    }

private:
    ThreadPool()
    {
        tasks_count_ = 0;
        stop_pool_ = false;
        thread_number_ = (int) sysconf(_SC_NPROCESSORS_ONLN);
        for (int i = 0; i < thread_number_; ++i)
        {
            std::thread thread([this](int ct) {
                printf("thread %2d created\n", ct);
                while (!stop_pool_)
                {
                    std::unique_lock<std::mutex> locker(mutex_);
                    cond_.wait(locker, [this](){ return !task_queue_.empty() || stop_pool_;});
                    if (stop_pool_)
                        break;
                    
                    std::function<void()> func = task_queue_.front();
                    task_queue_.pop();
                    --tasks_count_;
                    locker.unlock();
                    func();
                }
                printf("thread %2d out\n", ct);
                // std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            }, i);
            thread.detach();
        }
        printf("thread pool created\n");
    }

    ~ThreadPool()
    {

        stop_pool_ = true;
        cond_.notify_all();
        printf("thread pool destroyed\n");
    }

    bool stop_pool_;                                // 线程池是否停止
    const int MAX_TASKS_ = 30000;                   // 队列中最大任务数
    int tasks_count_;                               // 队列中任务数
    int thread_number_;                             // 线程池中线程数
    std::queue<std::function<void()>> task_queue_;  // 任务队列
    std::mutex mutex_;                              // 用于互斥访问的锁 
    std::condition_variable cond_;                  // 用于互斥访问的条件变量
};

#endif
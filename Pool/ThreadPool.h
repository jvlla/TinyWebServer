#ifndef __THREAD_POOL__
#define __THREAD_POOL__

#include <list>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <memory>
#include <stdio.h>

template<typename T>
class ThreadPool {
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool()
    {

        stop_pool_ = true;
        cond_.notify_all();
        printf("thread pool destroyed\n");
    }

    static ThreadPool<T>& get_instance() 
    {
        static ThreadPool<T> instance;
        return instance;
	}

    bool add_task(T * t)
    {
        std::unique_lock<std::mutex> locker(mutex_);
        if (tasks_count_ < MAX_TASKS_)
        {
            task_list_.push_back(t);
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
            std::thread thread([](int ct) {
                printf("thread %2d created\n", ct);
                while (!stop_pool_)
                {
                    std::unique_lock<std::mutex> locker(mutex_);
                    cond_.wait(locker, [](){ return !task_list_.empty() || stop_pool_;});
                    if (stop_pool_)
                        break;
                                  
                    T * t = task_list_.front();
                    task_list_.pop_front();
                    --tasks_count_;
                    locker.unlock();
                    t->read_and_process();
                }
                printf("thread %2d out\n", ct);
                // std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            }, i);
            thread.detach();
        }
        printf("thread pool created\n");
    }

    static bool stop_pool_;                 // ?????????????????????
    static const int MAX_TASKS_ = 30000;    // ????????????????????????
    static int tasks_count_;                // ??????????????????
    static int thread_number_;              // ?????????????????????
    /* ????????????????????????????????????????????????????????????????????? */
    static std::list<T *> task_list_;       // ????????????
    static std::mutex mutex_;               // ???????????????????????? 
    static std::condition_variable cond_;   // ?????????????????????????????????
};

template<typename T> bool ThreadPool<T>::stop_pool_;
template<typename T> int ThreadPool<T>::tasks_count_;
template<typename T> int ThreadPool<T>::thread_number_;
template<typename T> std::list<T *> ThreadPool<T>::task_list_;
template<typename T> std::mutex ThreadPool<T>::mutex_;
template<typename T> std::condition_variable ThreadPool<T>::cond_;

#endif
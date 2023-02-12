#ifndef __THREAD_POOL__
#define __THREAD_POOL__

#include <queue>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
/* ---------需要改------------
 * 先最原始的存个类然后回调函数，等后面研究明白function和future再继续搞，反正差不多
 */ 
template<typename T>
class ThreadPool {
public:
    ~ThreadPool()
    {
        //---------这里要设置stop，然后线程运行时也要判断然后挑出循环---------------
    }

    static ThreadPool * get_instance() 
    {
		static ThreadPool instance;
        //----------------这里还得初始化什么的--------------------
		return &instance;
	}

    bool add_task()
    {
        return true;
    }

private:
    ThreadPool();
    ThreadPool(const ThreadPool&);
    ThreadPool& operator=(const ThreadPool&);

    bool stop_pool_;
    int thread_number;              // 线程池中线程数
    std::queue<std::shared_ptr<T>> task_queue;       // 任务队列
    std::mutex mutex;               // 用于互斥访问的锁 
    std::condition_variable cond;   // 用于互斥访问的条件变量
};

#endif
#ifndef __TIMER__
#define __TIMER__

#include<vector>
#include<list>
#include<memory>

template<typename T>
class Timer{
public:
    Timer(): wheel_size(10) {}
    // 参数要接受间隔时间
    // 要注册一个信号处理函数,最后要alarm
    Timer(Timer &timer);
    Timer& operator=(Timer &timer);
    // 这个东西还是得表现得像值，不能是指针
    ~Timer() {}

    void tick();
    bool add(std::shared_ptr<T>);  // 添加新的定时事件
private:
    void addsig();  // 添加中断处理函数

    class SubTimer {
    public:
        SubTimer() {}  // 得提供回调函数
        ~SubTimer() {}  // 这个要不要特别处理啊
    private:
        std::shared_ptr<T> conn;
        // 这个好像还要move传递，就是调用httpconn.close()完事
    };
    std::vector<std::list<SubTimer>> wheel; // 时间轮，大小为wheel_size
    const int wheel_size;                   // 时间轮总槽数
    int curr_solt;                          // 时间轮当前槽位置，0到wheel_size-1
};

#endif
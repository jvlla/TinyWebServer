#ifndef __TIMER_WHEEL__
#define __TIMER_WHEEL__

#include <vector>
#include <unordered_map>
#include <functional>

template<typename T>
class TimerWheel{
public:
    TimerWheel(int slot_interval = 5, int wheel_size = 60): wheel_(wheel_size), slot_interval_(slot_interval), curr_solt_(0){}

    /* 时间轮转动 */
    void tick();
    /* 添加定时器，参数为超时时间、回调函数参数（唯一）和回调函数 */
    bool add(int timeout, T t, std::function<void(T)> cb_func);
    /* 删除定时器 */
    void del(T t);
    /* 由于依靠回调函数的参数t确定定时器，因此参数t不能重复 */
    bool is_duplicated_t(T t);
    /* 获得时间轮每槽间间隔时间 */
    int get_slot_interval();
private:
    /* 单个定时器 */
    struct Timer {
        Timer(int rotation, std::function<void(T)> cb_func): rotation_(rotation), cb_func_(cb_func) {}
        int rotation_;
        std::function<void(T)> cb_func_;
    };

    std::vector<std::unordered_map<T, Timer>> wheel_;   // 时间轮
    std::unordered_map<T, int> rec_;                    // 参数t对应
    const int slot_interval_;                           // 每槽间间隔时间
    int curr_solt_;                                     // 时间轮当前槽位置，0到wheel_size-1
};

#include "TimerWheel.tpp"  // 通过在头文件中包含.tpp文件实现模板类声明实现分离

#endif
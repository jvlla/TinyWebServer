#include "TimerWheel.h"
using namespace std;

template<typename T>
void TimerWheel<T>::tick()
{
    for (auto it = wheel_[curr_solt_].begin(); it != wheel_[curr_solt_].end();)
    {
        if (it->second.rotation_ > 0)
        {
            --(it->second.rotation_);
            ++it;
        }
        else
        {
            it->second.cb_func_(it->first);
            rec_.erase(it->first);
            it = wheel_[curr_solt_].erase(it);
        }
    }

    ++curr_solt_;
    curr_solt_ %= wheel_.size();
}

template<typename T>
bool TimerWheel<T>::add(int timeout, T t, std::function<void(T)> cb_func)
{
    if (is_duplicated_t(t) || timeout < 0)
        return false;
    int ticks, rotation, slot;
    int wheel_size = wheel_.size();

    /* 计算定时器滴答数、轮数和位于哪一时间槽 */
    if (timeout < slot_interval_)
        ticks = 1;
    else
        ticks = timeout / slot_interval_;
    rotation = ticks / wheel_size;
    slot = (curr_solt_ + ticks % wheel_size) % wheel_size;

    /* 将定时器插入时间轮 */
    wheel_[slot].emplace(t, Timer(rotation, cb_func));
    rec_[t] = slot;

    return true;
}

template<typename T>
void TimerWheel<T>::del(T t)
{
    wheel_[rec_[t]].erase(t);
    rec_.erase(t);
}

template<typename T>
bool TimerWheel<T>::is_duplicated_t(T t)
{
    return rec_.find(t) != rec_.end();
}

template<typename T>
int TimerWheel<T>::get_slot_interval()
{
    return slot_interval_;
}

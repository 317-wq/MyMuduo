#include "net/Timer.h"

Timer::Timer(TimePoint expire,
             Callback cb,
             bool repeat,
             Duration interval)
    :_expire(expire)
    ,_callback(std::move(cb))
    ,_repeat(repeat)
    ,_interval(interval)
{
}

// 是否到期
bool Timer::Expired() const{
    return Clock::now() >= _expire;
}

// 是否循环
bool Timer::Repeat() const{
    return _repeat;
}

// 获取超时时间点
Timer::TimePoint Timer::ExpireTimePoint() const{
    return _expire;
}

// 重置到期时间点[用于循环事件]
void Timer::Restart(){
    if(!_repeat)
        return;

    // 防止时间漂移
    _expire += _interval;
}

// 执行任务
void Timer::Run(){
    if(_callback)
        _callback();
}

Timer::~Timer() = default;
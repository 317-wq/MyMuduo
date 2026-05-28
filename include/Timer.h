#pragma once

/*
    单个定时器

    职责：
    1. 保存超时时间
    2. 保存回调任务
    3. 支持一次性任务
    4. 支持周期任务
*/

#include <functional>
#include <chrono>

class Timer{
public:
    using Callback = std::function<void()>; // 回调函数
    using Clock = std::chrono::steady_clock; // 稳定计时器
    using TimePoint = Clock::time_point; // 时间点
    using Duration = std::chrono::milliseconds; // 时间间隔ms级

private:
    TimePoint _expire; // 到期时间点
    Callback _callback; // 时间到了后的回调函数设置
    bool _repeat; // 是否重复
    Duration _interval; // 循环时间间隔

public:
    Timer(TimePoint expire,
          Callback cb,
          bool repeat = false,
          Duration interval = Duration(0));

    // 是否到期
    bool Expired() const;

    // 是否循环
    bool Repeat() const;

    // 获取超时时间点
    TimePoint ExpireTimePoint() const;
    
    // 重置到期时间点[用于循环事件]
    void Restart();

    // 执行任务
    void Run();

    ~Timer();
};
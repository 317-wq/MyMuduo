#pragma once

/*
    定时器管理模块
    职责：
    1. 管理多个Timer对象
    2. 使用timerfd接入epoll
    3. 管理一次性定时任务
    4. 管理周期定时任务
    5. 当时间到达后执行对应回调
*/

#include "net/Timer.h"
#include "net/Channel.h"
#include "net/EventLoop.h"
#include <set>
#include <memory>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstring>

class TimerQueue{
public:
    using TimerPtr = std::shared_ptr<Timer>;

private:
    EventLoop* _loop; // 定时器模块归哪一个loop管理
    int _timerfd; // 定时器的文件描述符
    std::unique_ptr<Channel> _channel; // 管理_timerfd的事件

    // 按照到期时间点排升序
    struct Compare{
        bool operator()(const TimerPtr &t1, const TimerPtr &t2) const;
    };

    std::set<TimerPtr, Compare> _timers; // 管理定时器

private:
    // 创建Timerfd
    int CreateTimerFd();

    // 绑定Timer的超时事件回调
    void HandleRead();

    // 更新最近超时时间，每次有新的定时器加入，最近的那个定时器作为读事件的触发条件
    // 就是timerfd的下一次超时时间
    void ResetTimerFd();

public:
    TimerQueue(EventLoop *loop);

    // 添加定时器
    void AddTimer(TimerPtr timer);

    // 一次性任务[sec秒之后执行]
    void RunAfter(int sec, Timer::Callback cb);

    // 周期性任务,repeat为true
    void RunEvery(int sec, Timer::Callback cb);

    ~TimerQueue();
};
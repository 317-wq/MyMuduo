#pragma once

#include <memory>
#include "Poller.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <sys/eventfd.h>
#include <memory>

class EventLoop{
public:
    using Ptr = std::shared_ptr<EventLoop>;
    using u32 = uint32_t;
    using u64 = uint64_t; // eventfd 默认一次是8个字节
    using ChannelList = std::vector<Channel*>;
    using Functor = std::function<void()>;

private:
    std::atomic<bool> _running;
    Poller::Ptr _poller; // 管理所有channels监听对象，监听事件
    ChannelList _active_channels; // 活跃连接（就绪的对应监听对象的监听事件）
    std::thread::id _thread_id;
    int _wakeup_fd; // 唤醒线程fd
    std::unique_ptr<Channel> _wakeup_channel; // 只有一个拥有者
    std::vector<Functor> _pending_functors; // 任务队列
    std::mutex _mutex;

private:
    int CreateEventFd() const;

    // EventLoop调用，收信号
    void HandleRead();

    // 其他线程调用，发信号
    void WakeUp();
    
public:
    EventLoop();
    
    void UpdateChannel(Channel* channel);

    void Loop();

    // 添加 Channel 并设置其 loop 指针
    // 用 UpdateChannel 替换 AddChannel
    // void AddChannel(Channel* channel);

    // 删除 Channel
    void RemoveChannel(Channel* channel);

    // 事件等待 + 返回活跃连接
    void Poll(int timeout);

    // 判断线程是否一致
    bool IsInLoopThread() const;

    // 执行任务队列
    void DoPendindFunctors();

    // 存储进任务队列
    void QueueInLoop(Functor func);

    // 直接运行
    void RunInLoop(Functor func);

    ~EventLoop();
};
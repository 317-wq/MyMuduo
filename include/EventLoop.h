#pragma once

#include <memory>
#include "Poller.h"

class EventLoop{
public:
    using Ptr = std::shared_ptr<EventLoop>;
    using u32 = uint32_t;
    using ChannelList = std::vector<Channel*>;

private:
    bool _running;
    Poller::Ptr _poller; // 管理所有channels监听对象，监听事件
    ChannelList _active_channels; // 活跃连接（就绪的对应监听对象的监听事件）

public:
    EventLoop();
    void UpdateChannel(Channel* channel);

    // 添加 Channel 并设置其 loop 指针
    void AddChannel(Channel* channel);

    Poller::Ptr GetPoller() const { return _poller; }

    ~EventLoop();
};
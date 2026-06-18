#pragma once

#include "Poller.h"
#include "Channel.h"
#include <cstdio>

#include <unordered_map>
#include <unistd.h>

// 继承父类Poller
class EpollPoller : public Poller {
private:
    inline static constexpr int INIT_EVENT = 64;

private:
    int _epfd;
    std::unordered_map<int, Channel*> _channels;
    // 一个epoll_event代表一个事件，可是一个监视对象，可能有多个事件完成
    std::vector<epoll_event> _events;

private:
    // 判断监听对象是否存在hash表中
    bool ExistChannel(Channel* channel) const;
    
    // 单独封装epoll_ctl
    void Update(int fd, Channel* channel);

    // 添加监听对象
    void AddChannel(Channel* channel);
    
public:
    EpollPoller();

    // 事件等待 + 存储活跃连接
    /*
        timeout：
        -1 : 永久阻塞
        0 : 立即返回
        >0 : 超时时间
    */
    void Poll(ChannelList* active_channels, int timeout);

    // 更新监听对象
    void UpdateChannel(Channel* channel);

    // 删除监听对象
    void RemoveChannel(Channel* channel);

    ~EpollPoller();

    // 存储活跃连接
    void FillActiveChannels(ChannelList* active_channels, int num);
};
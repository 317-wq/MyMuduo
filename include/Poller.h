#pragma once

/*
    接口抽象(父类)
    封装IO多路复用接口
    返回活跃连接
    管理监听对象
*/

#include "Channel.h"
#include <vector>
#include <memory>

class Poller{
public:
    // 存储活跃连接
    using ChannelList = std::vector<Channel*>;
    using Ptr = std::shared_ptr<Poller>;

public:
    Poller();
    
    // 事件等待 + 存储活跃连接
    virtual void Poll(int timeout) = 0;

    // 添加监听对象
    virtual void AddChannel() = 0;

    // 更新监听对象
    virtual void UpdateChannel() = 0;

    // 删除监听对象
    virtual void RemoveChannel() = 0;

    virtual ~Poller();
};
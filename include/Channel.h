#pragma once

/*
    对于某一个监听对象的事件的管理，设置，启动，关闭等，不需要进行事件更新的操作
    管理事件回调
    通知EventLoop更新事件
*/

#include <functional>
#include <memory>
#include <sys/epoll.h>

class EventLoop;

class Channel{
private:
    using EventCallback = std::function<void()>;
    using u32 = uint32_t;

public:
    using Ptr = std::shared_ptr<Channel>;

private:
    int _fd;
    u32 _events; // 关心的事件
    u32 _revents;  // 触发的事件
    EventCallback _read_cb; // 读事件回调
    EventCallback _write_cb; // 写事件回调
    EventCallback _error_cb; // 错误事件回调
    EventCallback _close_cb; // 关闭事件回调
    EventCallback _event_cb; // 任意事件回调

    EventLoop* _loop; // 后续将数据注册到内核里面，就是职责分开，实际上还是调用epoll_ctl
public:
    // explicit Channel(int fd);

    Channel(EventLoop* loop, int fd);

    int Fd() const;
    u32 Events() const;
    u32 Revents() const;

    void SetREvents(u32 event);

    void SetReadCallback(EventCallback read_cb);
    void SetWriteCallback(EventCallback write_cb);
    void SetErrorCallback(EventCallback error_cb);
    void SetCloseCallback(EventCallback close_cb);
    void SetEventCallback(EventCallback event_cb);

    // 注册事件，同时注册到内核里面
    void EnableRead();
    void EnableWrite();

    void DisableRead();
    void DisableWrite();
    void DisableAll();

    bool ReadAble() const;
    bool WriteAble() const;

    // 根据_revents处理事件
    void HandleEvent();

    // EventLoop来管理这些操作
    void Update();

    // 设置所属的 EventLoop
    // void SetLoop(EventLoop* loop);

    ~Channel();
};
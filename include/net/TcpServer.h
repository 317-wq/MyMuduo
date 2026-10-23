#pragma once

#include "net/EventLoop.h"
#include "net/EventLoopThreadPool.h"
#include "net/Acceptor.h"
#include "net/TcpConnection.h"
#include "net/TimeWheel.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <functional>
#include <unordered_map>

// one thread one loop

// 管理多个连接对象以及连接任务等

class TcpServer{
public:
    using u16 = uint16_t;
    using ConnectCallback = std::function<void(const TcpConnection::Ptr &)>; // 连接事件回调
    // 消息事件回调 -> 回调自身函数，对下层传输过来的数据进行处理
    using MessageCallback = std::function<void(const TcpConnection::Ptr &, Buffer *)>;
    // 连接关闭通知，在移除连接后由 base_loop 调用，业务层不应覆盖底层 close 回调
    using CloseNotifyCallback = std::function<void(const TcpConnection::Ptr &)>;

private:
    EventLoop* _base_loop; // 事件循环-主线程
    std::unique_ptr<EventLoopThreadPool> _thread_pool; // 管理任务线程对象池
    std::unique_ptr<Acceptor> _acceptor; // 管理新连接接收
    // 一个服务器管理多个新accept的连接对象
    std::unordered_map<int, TcpConnection::Ptr> _connections;
    ConnectCallback _connect_cb; // 通知上层连接到来
    MessageCallback _message_cb; // 通知上层有数据
    CloseNotifyCallback _close_notify_cb; // 通知上层连接已关闭
    std::unique_ptr<TimeWheel> _time_wheel;
    std::atomic<size_t> _connection_count{0}; // 当前连接数，供监控和测试读取

private:
    // 连接是否存在
    bool ExistConnection(int fd);
    
    // 增加新连接
    void AddConnection(int fd);

    // 删除连接
    void RemoveConnection(const TcpConnection::Ptr& conn);

    // 将增加新连接，建立连接的函数进行绑定
    void SetAddConnectionCallback();

    // 用于绑定时间轮的回调函数
    void HandleTimeout(int fd);

    // 用于TcpConnection的message的事件回调绑定，需要让wheel也知道连接的进行，要不然就直接销毁了
    void OnMessage(const TcpConnection::Ptr &conn, Buffer *buf);

    // 在主线程里面删除
    void RemoveConnectionInLoop(const TcpConnection::Ptr &conn);

public:
    TcpServer(EventLoop* base_loop, u16 port, size_t thread_num, size_t timeout);

    // 上层设置[chatserver设置，实际的业务层]
    void SetMessageCallback(MessageCallback cb);

    void SetConnectCallback(ConnectCallback cb);

    // 设置连接关闭通知；底层 close 回调仍由 TcpServer 自己维护。
    void SetCloseNotifyCallback(CloseNotifyCallback cb);

    // 当前连接数（测试与监控用）。
    size_t ConnectionCount() const { return _connection_count.load(); }

    // 实际监听端口（构造时传 0 时由内核分配）。
    u16 Port() const { return _acceptor->Port(); }

    // 运行
    void Start();

    // 删除新连接
    ~TcpServer();
};

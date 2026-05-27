#pragma once

#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "Acceptor.h"
#include "TcpConnection.h"
#include <memory>
#include <functional>
#include <unordered_map>

// one thread one loop

class TcpServer{
public:
    using u16 = uint16_t;
    using ConnectCallback = std::function<void(const TcpConnection::Ptr &)>; // 连接事件回调
    // 消息事件回调 -> 回调自身函数，对下层传输过来的数据进行处理
    using MessageCallback = std::function<void(const TcpConnection::Ptr &, Buffer *)>;

private:
    EventLoop* _base_loop; // 事件循环-主线程
    std::unique_ptr<EventLoopThreadPool> _thread_pool; // 管理任务线程对象池
    std::unique_ptr<Acceptor> _acceptor; // 管理新连接接收
    // 一个服务器管理多个新accept的连接对象
    std::unordered_map<int, TcpConnection::Ptr> _connections;
    ConnectCallback _connect_cb; // 通知上层连接到来
    MessageCallback _message_cb; // 通知上层有数据

private:
    // 连接是否存在
    bool ExistConnection(int fd);
    
    // 增加新连接
    void AddConnection(int fd);

    // 删除连接
    void RemoveConnection(const TcpConnection::Ptr& conn);

    // 将增加新连接，建立连接的函数进行绑定
    void SetAddConnectionCallback();
    
public:
    TcpServer(EventLoop* base_loop, u16 port, size_t thread_num);

    // 上层设置
    void SetMessageCallback(MessageCallback cb);

    void SetConnectCallback(ConnectCallback cb); 

    // 运行
    void Start();

    // 删除新连接
    ~TcpServer();
};
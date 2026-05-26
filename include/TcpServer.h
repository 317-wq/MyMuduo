#pragma once

#include "EventLoop.h"
#include "Acceptor.h"
#include "TcpConnection.h"
#include <memory>
#include <functional>
#include <unordered_map>

// one thread one loop

class TcpServer{
private:
    EventLoop* _loop; // 事件循环
    std::unique_ptr<Acceptor> _acceptor; // 管理新连接接收
    // 一个服务器管理多个新accept的连接对象
    std::unordered_map<int, TcpConnection::Ptr> _connections;

public:  
    using u16 = uint16_t;

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
    TcpServer(EventLoop* loop, u16 port);

    // 运行
    void Start();

    // 删除新连接
    ~TcpServer();
};
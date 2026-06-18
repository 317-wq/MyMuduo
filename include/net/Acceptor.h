#pragma once

/*
    创建一个专门用于监听新连接的对象
    根据connfd关联对应的创建connetcion的回调，不负责实际创建
*/

#include "net/Socket.h"
#include "net/EventLoop.h"

class Acceptor {
public:
    using u16 = uint16_t;
    using NewConnectCallback = std::function<void(int)>; // fd

private:
    EventLoop* _loop; // 都属于loop的使用，管理
    std::unique_ptr<Socket> _listen_sock;
    std::unique_ptr<Channel> _channel; // 管理sockfd的事件
    NewConnectCallback _new_connection_cb; // 新连接事件回调，通知上层

private:
    // 绑定读回调
    void HandleRead();

public:
    // 初始化监听对象 -> true为非阻塞
    Acceptor(EventLoop* loop, const InetAddress &local, bool non_block = true);
    Acceptor(EventLoop* loop, u16 port, bool non_block = true);

    // 返回新连接的文件描述符fd
    int Accept(InetAddress *client);
    int Accept(std::string *client_ip, u16 *client_port);

    // 获取监听socket的fd
    int Fd() const;

    // 开始监听
    void Listen();

    // 绑定创建连接回调
    void SetAddConnectionCallback(NewConnectCallback cb);

    ~Acceptor();
};
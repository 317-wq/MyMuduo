#include "net/Acceptor.h"
#include <sys/socket.h>
#include <iostream>
#include <stdexcept>
#include <string>

// 监听读事件：ET 模式下必须一次把 accept 队列排空，否则剩余连接可能等不到下一次边沿。
void Acceptor::HandleRead(){
    InetAddress client;
    for (;;) {
        int connfd = Accept(&client);
        if (connfd < 0) {
            break;
        }
        // 通知上层创建连接
        if (_new_connection_cb)
            _new_connection_cb(connfd);
    }
}

// 初始化监听对象
Acceptor::Acceptor(EventLoop* loop, const InetAddress &local, bool non_block)
    : _loop(loop)
    , _listen_sock(std::make_unique<Socket>())
    , _channel(std::make_unique<Channel>(loop, _listen_sock->Fd()))
{
    _listen_sock->SetReuseAddr();
    // 单实例服务只保留 SO_REUSEADDR（处理 TIME_WAIT）；不开启 SO_REUSEPORT，
    // 否则第二个实例会静默共享监听端口，导致“假启动”。
    if (non_block)
        _listen_sock->SetNonBlock();
    // 绑定/监听失败必须向上抛错，避免端口被占用时服务端“假启动”。
    if (!_listen_sock->Bind(local)) {
        throw std::runtime_error("Acceptor: bind failed on port "
                                 + std::to_string(local.Port()));
    }
    if (!_listen_sock->Listen()) {
        throw std::runtime_error("Acceptor: listen failed on port "
                                 + std::to_string(local.Port()));
    }
    // 设置关心读事件回调
    _channel->SetReadCallback(std::bind(&Acceptor::HandleRead, this));
}

Acceptor::Acceptor(EventLoop* loop, Acceptor::u16 port, bool non_block)
    : Acceptor(loop, InetAddress(port), non_block)
    {}

// 返回新连接的文件描述符fd
int Acceptor::Accept(InetAddress *client){
    return _listen_sock->Accept(client);
}

int Acceptor::Accept(std::string *client_ip, Acceptor::u16 *client_port){
    return _listen_sock->Accept(client_ip, client_port);
}

// 获取监听socket的fd
int Acceptor::Fd() const { 
    return _listen_sock->Fd(); 
}

// 获取内核实际分配的监听端口，便于测试和日志展示。
Acceptor::u16 Acceptor::Port() const {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(_listen_sock->Fd(), reinterpret_cast<sockaddr*>(&addr), &len) < 0)
        return 0;
    return ntohs(addr.sin_port);
}

// 绑定创建回调
void Acceptor::SetAddConnectionCallback(NewConnectCallback cb){
    _new_connection_cb = std::move(cb);
}

// 将监听套接字绑定到epoll内核里面
void Acceptor::Listen(){
    _channel->EnableRead();
}

Acceptor::~Acceptor() = default;

#include "../include/Acceptor.h"
#include <iostream>

// 监听读事件，有新连接到来通知上层
void Acceptor::HandleRead(){
    InetAddress client;
    int connfd = Accept(&client);
    if(connfd < 0)
        return;
    // 通知上层创建连接
    if(_new_connection_cb)
        _new_connection_cb(connfd);
}

// 初始化监听对象
Acceptor::Acceptor(EventLoop* loop, const InetAddress &local, bool non_block)
    : _loop(loop)
    , _listen_sock(std::make_unique<Socket>())
    , _channel(std::make_unique<Channel>(loop, _listen_sock->Fd()))
{
    _listen_sock->SetReuseAddr();
    _listen_sock->SetReusePort();
    if (non_block)
        _listen_sock->SetNonBlock();
    _listen_sock->Bind(local);
    _listen_sock->Listen();
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

// 绑定创建回调
void Acceptor::SetAddConnectionCallback(NewConnectCallback cb){
    _new_connection_cb = std::move(cb);
}

// 将监听套接字绑定到epoll内核里面
void Acceptor::Listen(){
    _channel->EnableRead();
}

Acceptor::~Acceptor() = default;
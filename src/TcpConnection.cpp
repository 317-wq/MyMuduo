#include "../include/TcpConnection.h"

// 读事件绑定
void TcpConnection::HandleRead() {}

// 写事件绑定
void TcpConnection::HandleWrite() {}

// 关闭事件绑定
void TcpConnection::HandleClose() {}

// 错误事件绑定
void TcpConnection::HandleError() {}

TcpConnection::TcpConnection(EventLoop* loop, int fd)
    :_loop(loop)
    ,_channel(std::make_unique<Channel>(loop, fd))
{
    // 设置_channel的回调函数
    _channel->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
    _channel->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
    _channel->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
    _channel->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

int TcpConnection::Fd() const{
    if(_channel)
        return _channel->Fd();
}

// 建立连接
void TcpConnection::ConnectEstablished(){
    _channel->EnableRead();
    if(_connect_cb)
        _connect_cb(shared_from_this());
}

// 设置TcpConnection层的回调
void TcpConnection::SetConnectCallback(ConnectCallback cb) { _connect_cb = std::move(cb); }
void TcpConnection::SetMessageCallback(MessageCallback cb) { _message_cb = std::move(cb); }
void TcpConnection::SetCloseCallback(CloseCallback cb) { _close_cb = std::move(cb); }

// 销毁连接
void TcpConnection::ConnectDestroyed(){
    _channel->DisableAll();
    if(_close_cb)
        _close_cb(shared_from_this());
    // 从内核和hash里面去除
    _channel->Remove();
}

TcpConnection::~TcpConnection() = default;
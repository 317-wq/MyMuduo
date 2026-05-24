#include "../include/TcpConnection.h"

// 读事件绑定
void TcpConnection::HandleRead() {
    char buffer[1024];
    int n = recv(Fd(), buffer, 1023, 0);
    if(n == 0){
        HandleClose();
        return;
    }
    if(n < 0){
        HandleError();
        return;
    }

    buffer[n] = 0;
    std::cout << "echo@ " << buffer << std::endl;
    if(_message_cb){
        _message_cb(shared_from_this());
        return;
    }
}

// 写事件绑定
void TcpConnection::HandleWrite() {}

// 关闭事件绑定
void TcpConnection::HandleClose() { ConnectDestroyed(); }

// 错误事件绑定
void TcpConnection::HandleError() {
    std::cerr << "fd: " << _socket->Fd() << std::endl;
    HandleClose(); // 关闭
}

// 最终释放 
void TcpConnection::ConnectDestroyed(){
    _channel->DisableAll(); // 事件全部关闭
    if(_close_cb)
        _close_cb(shared_from_this());
    _channel->Remove(); // 内核中和hash中除去
}

TcpConnection::TcpConnection(EventLoop* loop, int fd)
    :_loop(loop)
    ,_socket(std::make_unique<Socket>(fd))
    ,_channel(std::make_unique<Channel>(loop, fd))
{
    // 设置_channel的回调函数
    _channel->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
    _channel->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
    _channel->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
    _channel->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

int TcpConnection::Fd() const{
    if(_socket)
        return _socket->Fd();
}

// 建立连接
void TcpConnection::ConnectEstablished(){
    _channel->EnableRead();
    if(_connect_cb)
        _connect_cb(shared_from_this());
}

void TcpConnection::Send(const std::string &str){
    send(Fd(), str.c_str(), str.size(), 0);
    // ...
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
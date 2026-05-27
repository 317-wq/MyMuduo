#include "../include/TcpConnection.h"

// 读事件绑定
void TcpConnection::HandleRead() {
    char buffer[4096];
    while (true){
        int n = recv(Fd(), buffer, sizeof(buffer) - 1, 0);
        if(n > 0){
            // 信息输入到接收缓冲区里面
            _in_buffer.Append(buffer, n);
        }
        else if(n == 0){
            HandleClose();
            return;
        }
        else{
            if(errno == EWOULDBLOCK || errno == EAGAIN)
                break;
            HandleError();
            return;
        }
    }

    // std::cout << "echo@ " << buffer;
    // 上层处理inbuffer里的数据
    if(_message_cb){
        _message_cb(shared_from_this(), &_in_buffer);
        return;
    }
}

// 连接是否建立完成
bool TcpConnection::Connected() const {
    return _state == CONNECTED;
}

// 连接是否已经关闭
bool TcpConnection::DisConnected() const {
    return _state == DISCONNECTED;
}

// 写事件绑定
void TcpConnection::HandleWrite() {
    ssize_t readable = _out_buffer.ReadableSize();
    if(readable == 0){
        _channel->DisableWrite();
        return;
    }
    
    ssize_t n = send(Fd(), _out_buffer.Peek(), readable, 0);
    if(n < 0){
        if(errno == EWOULDBLOCK || errno == EAGAIN)
            return;
        HandleError();
        return;
    }
    if(n == 0){
        HandleClose();
        return;
    }

    // 取出固定长度的字节数
    std::string str = _out_buffer.Retrieve(n);
    std::cout << str; // 回显看一下
    // 此时发送缓冲区里面没有数据要发送，关闭写事件监听
    if(_out_buffer.ReadableSize() == 0)
        _channel->DisableWrite();
}

// 关闭连接事件绑定
void TcpConnection::HandleClose() { 
    if(_state == DISCONNECTED)
        return;
    _state = DISCONNECTED;
    if(_close_cb)
        _close_cb(shared_from_this());
}

// 错误事件绑定
void TcpConnection::HandleError() {
    std::cerr << "fd: " << _socket->Fd() << std::endl;
    HandleClose(); // 关闭
}

// 获取inbuffer的起始位置
Buffer *TcpConnection::InBuffer(){
    return &_in_buffer;
}

// 获取outbuffer的起始位置
Buffer *TcpConnection::OutBuffer(){
    return &_out_buffer;
}

// 最终释放 
void TcpConnection::ConnectDestroyed(){
    _channel->DisableAll(); // 事件全部关闭
    _channel->Remove(); // 内核中和hash中除去
    _socket->Close(); // 关闭套接字
}

TcpConnection::TcpConnection(EventLoop* loop, int fd, bool non_block)
    :_loop(loop)
    ,_socket(std::make_unique<Socket>(fd))
    ,_channel(std::make_unique<Channel>(loop, fd))
    ,_state(CONNECTING)
{
    // 根据需要设置非阻塞
    if(non_block && _socket)
        _socket->SetNonBlock();

    // 设置_channel的回调函数
    _channel->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
    _channel->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
    _channel->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
    _channel->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

int TcpConnection::Fd() const{
    if(_socket){
        int fd = _socket->Fd();
        return fd;
    }
    return -1;
}

// 建立连接完成
void TcpConnection::ConnectEstablished(){
    _state = CONNECTED; // 连接建立完成状态
    _channel->EnableRead();
    if(_connect_cb)
        _connect_cb(shared_from_this());
}

void TcpConnection::Send(const std::string &str){
    // send(Fd(), str.c_str(), str.size(), 0);
    // 将处理好的数据输出到发送缓冲区
    _out_buffer.Append(str);
    // 启动写事件监听，如果客户端的接收缓冲区有空闲，那么我就可以将送法缓冲区里面的数据发送
    if(!_channel->WriteAble())
        _channel->EnableWrite();
}

// 设置TcpConnection层的上层业务回调
void TcpConnection::SetConnectCallback(ConnectCallback cb) { _connect_cb = std::move(cb); }
void TcpConnection::SetMessageCallback(MessageCallback cb) { _message_cb = std::move(cb); }
void TcpConnection::SetCloseCallback(CloseCallback cb) { _close_cb = std::move(cb); }

TcpConnection::~TcpConnection() = default;
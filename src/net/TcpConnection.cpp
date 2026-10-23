#include "net/TcpConnection.h"
#include "net/EventLoop.h"

#include <cerrno>
#include <sys/socket.h>

// 读事件绑定：ET 模式下循环 recv 到 EAGAIN；读到 0 时先把已读数据分发完再关闭。
void TcpConnection::HandleRead() {
    char buffer[4096];
    bool peer_closed = false;

    while (true){
        ssize_t n = recv(Fd(), buffer, sizeof(buffer) - 1, 0);
        if(n > 0){
            // 信息输入到接收缓冲区里面
            _in_buffer.Append(buffer, n);
            continue;
        }
        if(n == 0){
            // 对端半关闭：先记下，等缓冲区里的最后一批数据分发完再关闭。
            peer_closed = true;
            break;
        }
        if(errno == EWOULDBLOCK || errno == EAGAIN)
            break;
        HandleError();
        return;
    }

    // self 保证分发和后续关闭期间连接对象不会被析构。
    auto self = shared_from_this();
    if(_message_cb && _in_buffer.ReadableSize() > 0){
        _message_cb(self, &_in_buffer);
    }
    if(peer_closed){
        HandleClose();
    }
}

// 获取当前的loop
EventLoop *TcpConnection::GetLoop(){
    return _loop;
}

// 连接是否建立完成
bool TcpConnection::Connected() const {
    return _state == CONNECTED;
}

// 连接是否已经关闭
bool TcpConnection::DisConnected() const {
    return _state == DISCONNECTED;
}

// 写事件绑定：ET 模式下必须循环 send 到 EAGAIN，否则部分写后不会再收到 EPOLLOUT。
void TcpConnection::HandleWrite() {
    if(_out_buffer.ReadableSize() == 0){
        _channel->DisableWrite();
        return;
    }

    while (_out_buffer.ReadableSize() > 0) {
        ssize_t n = send(Fd(), _out_buffer.Peek(), _out_buffer.ReadableSize(), MSG_NOSIGNAL);
        if(n > 0){
            // 消费已发送的字节，继续尝试发送剩余数据
            _out_buffer.Retrieve(n);
            continue;
        }
        if(n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)){
            // 内核发送缓冲区已满，保留 EPOLLOUT 监听，等下一次可写事件
            return;
        }
        if(n == 0){
            HandleClose();
            return;
        }
        HandleError();
        return;
    }

    // 此时 out_buffer 已清空，关闭写事件监听
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

// 错误事件绑定 — socket 发生错误，直接关闭连接
void TcpConnection::HandleError() {
    HandleClose();
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
    _channel->EnableRead(); // 操作epoll，跨线程时候有危险
    if(_connect_cb)
        _connect_cb(shared_from_this());
}

// 上层调用，可能跨线程 → 分发到 io_loop 执行
void TcpConnection::Send(const std::string &str){
    _loop->RunInLoop([conn = shared_from_this(), str]{
        conn->SendInLoop(str);
    });
}

// 在 io_loop 线程真正执行发送
void TcpConnection::SendInLoop(const std::string &str){
    if(!Connected()) return;
    _out_buffer.Append(str);
    // 启动写事件监听，如果客户端的接收缓冲区有空闲就可以发送
    if (!_channel->WriteAble())
        _channel->EnableWrite();
}

// 设置TcpConnection层的上层业务回调[ChatServer设置]
void TcpConnection::SetConnectCallback(ConnectCallback cb) { _connect_cb = std::move(cb); }
void TcpConnection::SetMessageCallback(MessageCallback cb) { _message_cb = std::move(cb); }
void TcpConnection::SetCloseCallback(CloseCallback cb) { _close_cb = std::move(cb); }

TcpConnection::~TcpConnection() = default;

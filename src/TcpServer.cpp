#include "../include/TcpServer.h"

TcpServer::TcpServer(EventLoop *loop, u16 port)
    :_loop(loop)
    ,_acceptor(std::make_unique<Acceptor>(loop, port))
    {
        
    }

// 连接是否存在
bool TcpServer::ExistConnection(int fd){
    return _connections.find(fd) == _connections.end() ? false : true;
}

void TcpServer::SetMessageCallback(MessageCallback cb){
    _message_cb = std::move(cb);
}

void TcpServer::SetConnectCallback(ConnectCallback cb){
    _connect_cb = std::move(cb);
}

// 增加新连接 -> 实际做事
void TcpServer::AddConnection(int fd){
    // 在this的当前事件循环里面
    auto conn = std::make_shared<TcpConnection>(_loop, fd);

    // 设置该连接的事件回调
    conn->SetConnectCallback(_connect_cb);
    conn->SetMessageCallback(_message_cb);
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    _connections[fd] = conn;
    // 建立连接，回调函数，通知上层连接建立完成，转交后续执行
    conn->ConnectEstablished();
}

// 删除连接
void TcpServer::RemoveConnection(const TcpConnection::Ptr &conn){
    int fd = conn->Fd();
    if(!ExistConnection(fd)) return;
    _connections.erase(fd);
    conn->ConnectDestroyed(); // 释放连接
}

// 将增加新连接，建立连接的函数进行绑定
void TcpServer::SetAddConnectionCallback(){
    _acceptor->SetAddConnectionCallback(std::bind(&TcpServer::AddConnection, this, std::placeholders::_1));
}

void TcpServer::Start(){
    // 设置到acceptor里面
    SetAddConnectionCallback();
    _acceptor->Listen();
}

TcpServer::~TcpServer() = default;
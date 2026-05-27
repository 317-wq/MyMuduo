#include "../include/TcpServer.h"

TcpServer::TcpServer(EventLoop *base_loop, u16 port, size_t thread_num)
    :_base_loop(base_loop)
    ,_thread_pool(std::make_unique<EventLoopThreadPool>(base_loop, thread_num))
    ,_acceptor(std::make_unique<Acceptor>(base_loop, port))
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
    // 在this的当前事件循环里面[这个直接由主线程构造连接是单线程版本]
    // auto conn = std::make_shared<TcpConnection>(_base_loop, fd);

    // 多线程
    EventLoop* io_loop = _thread_pool->GetNextLoop();
    // 调试输出
    std::cout << fd << " -> " << io_loop << std::endl;
    auto conn = std::make_shared<TcpConnection>(io_loop, fd);
    // 设置该连接的事件回调
    conn->SetConnectCallback(_connect_cb);
    conn->SetMessageCallback(_message_cb);
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    _connections[fd] = conn;
    // 建立连接，回调函数，通知上层连接建立完成，转交后续执行
    /*
        ConnectEstablished函数内部，有注册读事件，操作epoll
        应该属于work-thread自己的loop来执行[谁拥有loop，谁操作loop]
        这边交给base线程的话，就会引发跨线程操作问题
        
        TcpConnection正式归worker管理
        ->修改状态
        ->注册connfd到worker epoll
        ->通知业务上线
    */
    io_loop->RunInLoop([conn]{
        conn->ConnectEstablished();
    });
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

// 启动服务器，所需要的准备工作
void TcpServer::Start(){
    // 准备任务线程池
    _thread_pool->Start();
    // 设置到acceptor里面
    SetAddConnectionCallback();
    // 启动监听客户端fd
    _acceptor->Listen();
}

TcpServer::~TcpServer() = default;
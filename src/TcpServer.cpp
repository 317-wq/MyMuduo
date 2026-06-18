#include "../include/TcpServer.h"
namespace ph = std::placeholders;

// 绑定时间轮里面的超时回调，销毁连接（在 base_loop 线程执行）
void TcpServer::HandleTimeout(int fd){
    auto it = _connections.find(fd);
    if(it == _connections.end()){
        return;
    }
    // 当前已在 base_loop，直接调用 InLoop 版本
    RemoveConnectionInLoop(it->second);
}

// 用于TcpConnection的message的事件回调绑定，需要让wheel也知道连接的进行，要不然就直接销毁了
void TcpServer::OnMessage(const TcpConnection::Ptr &conn, Buffer *buf){
    // timewheel需要刷新这个链接的寿命
    _time_wheel->Refresh(conn->Fd());
    // 调用业务层，这个就是别人使用的时候需要先预先设置的
    if(_message_cb){
        _message_cb(conn, buf);
    }
}

TcpServer::TcpServer(EventLoop *base_loop, u16 port, size_t thread_num, size_t timeout)
    :_base_loop(base_loop)
    ,_thread_pool(std::make_unique<EventLoopThreadPool>(base_loop, thread_num))
    ,_acceptor(std::make_unique<Acceptor>(base_loop, port))
    ,_time_wheel(std::make_unique<TimeWheel>(base_loop, timeout))
    {
        // 预留一个fd参数位
        _time_wheel->SetTimeoutCallback(std::bind(&TcpServer::HandleTimeout, this, ph::_1));
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
    conn->SetMessageCallback(std::bind(&TcpServer::OnMessage, this, ph::_1, ph::_2));
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, ph::_1));

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
   _time_wheel->Insert(fd);
    io_loop->RunInLoop([conn]{
        conn->ConnectEstablished();
    });
}

// 在主线程里面删除（由 base_loop 调用）
void TcpServer::RemoveConnectionInLoop(const TcpConnection::Ptr &conn){
    int fd = conn->Fd();
    if(!ExistConnection(fd)) return;
    _time_wheel->Remove(fd);
    _connections.erase(fd);

    // 把 epoll 操作分发到连接归属的 io_loop 执行
    EventLoop* io_loop = conn->GetLoop();
    io_loop->RunInLoop([conn]{
        conn->ConnectDestroyed();
    });
}

// 删除连接（可能被 worker 线程通过 close 回调调用）
void TcpServer::RemoveConnection(const TcpConnection::Ptr &conn){
    // 分发到主线程统一处理 bookkeeping，避免 _connections 数据竞争
    _base_loop->RunInLoop([this, conn]{
        RemoveConnectionInLoop(conn);
    });
}

// 将增加新连接，建立连接的函数进行绑定
void TcpServer::SetAddConnectionCallback(){
    _acceptor->SetAddConnectionCallback(std::bind(&TcpServer::AddConnection, this, ph::_1));
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

TcpServer::~TcpServer(){
    // 1. 先停止时间轮（_alive 置 false，后续 Tick 回调不再执行）
    _time_wheel.reset();

    // 2. 拷贝连接列表到局部变量，再清空 map
    //    这样即使 worker 线程触发 HandleClose → RemoveConnection，
    //    ExistConnection 返回 false 也不会修改 _connections
    std::vector<TcpConnection::Ptr> conns;
    conns.reserve(_connections.size());
    for (auto &pair : _connections){
        conns.push_back(pair.second);
    }
    _connections.clear();

    // 3. 分发 ConnectDestroyed 到各 worker 线程
    for (auto &conn : conns){
        if (conn){
            EventLoop* io_loop = conn->GetLoop();
            io_loop->RunInLoop([conn]{
                conn->ConnectDestroyed();
            });
        }
    }

    // 4. thread_pool 析构：Quit → worker Loop 退出前执行 pending functors
    //    （包含上一步分发的 ConnectDestroyed）→ join 等待线程结束
    //    按成员声明逆序自动析构：先 _acceptor，最后 _thread_pool
}
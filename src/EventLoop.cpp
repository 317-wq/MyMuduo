#include "../include/EventLoop.h"
#include "../include/EpollPoller.h"

int EventLoop::CreateEventFd() const{
    // 禁止进程复制，设置非阻塞同时
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    return fd;
}

// EventLoop调用，收信号
void EventLoop::HandleRead(){
    u64 arg;
    read(_wakeup_fd, &arg, sizeof(arg));
}

// 其他线程调用，发信号
void EventLoop::WakeUp(){
    u64 arg = 1;
    // 内核自己管理 ++
    write(_wakeup_fd, &arg, sizeof(arg));
}

EventLoop::EventLoop()
    :_running(false)
    ,_poller(std::make_shared<EpollPoller>())
    ,_thread_id(std::this_thread::get_id())
    ,_wakeup_fd(CreateEventFd())
    ,_wakeup_channel(std::make_unique<Channel>(this, _wakeup_fd))
{  
    // 设置_wakeup_channel需要监控的事件等
    // 关连到内核里面，这样push，wakeup之后，poll状态转变，读取到任务
    _wakeup_channel->SetReadCallback(std::bind(&EventLoop::HandleRead, this));
    // 读事件注册到内核
    _wakeup_channel->EnableRead();
}

// 事件循环
void EventLoop::Loop(){
    _running = true;
    while(_running){
        _active_channels.clear();
        // 等待活跃连接
        Poll(3000);
        // 分发事件，处理监控事件的各自回调
        for(auto &channel : _active_channels){
            channel->HandleEvent();
        }
        // 执行任务
        DoPendingFunctors();
    }
}

void EventLoop::UpdateChannel(Channel* channel){
    if(_poller)
        _poller->UpdateChannel(channel);
}

// void EventLoop::AddChannel(Channel* channel){
//     // 设置 Channel 的 loop 指针，并添加到 poller
//     /*
//         这边的loop变量一开始是nullptr，如果不先赋值的话，就会导致对空指针进行访问
//         采用设计统一接口的方式，如果不这样设计的话，就是在channel里面将eventloop类
//         设置成friend，允许对私有成员变量进行直接操作
//     */
//     _poller->AddChannel(channel);
// }

// 删除 Channel
void EventLoop::RemoveChannel(Channel *channel){
    if(_poller)
        _poller->RemoveChannel(channel);
}

void EventLoop::Poll(int timeout){
    if(_poller)
        _poller->Poll(&_active_channels, timeout);
}

// 判断线程是否一致
bool EventLoop::IsInLoopThread() const{
    return _thread_id == std::this_thread::get_id();
}

// 执行任务队列
void EventLoop::DoPendingFunctors(){
    // 局部变量，线程私有
    FunctorList tasks;
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // _pending_functors是多线程的公共资源，为了避免资源竞争问题
        // 这边就直接交换资源
        tasks.swap(_pending_functors);
    }

    // 执行任务
    for(auto &f : tasks)
        f();
}

// 存储进任务队列 这个动作是在多线程的时候 需要上锁
void EventLoop::QueueInLoop(Functor func){
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _pending_functors.push_back(std::move(func));
    }
    // 计数器++，唤醒正在由于无事件就绪的线程结束阻塞
    WakeUp();
}

// 直接运行
void EventLoop::RunInLoop(Functor func){
    if(IsInLoopThread()) func();
    else QueueInLoop(std::move(func));
}

EventLoop::~EventLoop(){
    if(_wakeup_fd >= 0){
        close(_wakeup_fd);
        _wakeup_fd = -1;
    }
}
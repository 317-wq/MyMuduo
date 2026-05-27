#include "../include/EventLoopThread.h"

// 工作线程入口：创建工作线程，绑定loop，唤醒主线程
void EventLoopThread::ThreadFunc(){
    EventLoop loop;
    // 绑定事件循环
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _loop = &loop;
        // 通知主线程
        _cond.notify_one();
    }

    // workt-hread进行事件循环
    _loop->Loop();
    // 任务执行完成
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _loop = nullptr;
    }
}

EventLoopThread::EventLoopThread()
    :_loop(nullptr)
    ,_started(false)
    {}

// 返回线程当前绑定的loop，等待loop创建完成
EventLoop *EventLoopThread::StartLoop(){
    if(_started)
        return _loop;
    _started = true;

    // 工作线程绑定对应创建work线程的回调函数
    _thread = std::make_unique<std::thread>(&EventLoopThread::ThreadFunc, this);
    // 异步进行创建，需要条件变量控制，是都已经创建完成
    // 同步：主线程会暂时阻塞等待workloop创建完成
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this] {
            return _loop != nullptr;
        });
    }

    // 通知后，拿到对应的loop
    return _loop;
}

EventLoopThread::~EventLoopThread(){
    if(_thread && _thread->joinable()){
        _thread->join();
    }
}
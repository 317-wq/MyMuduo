#pragma once

/*
    每一个线程都有自己的事件循环 one thread one loop
*/

#include "net/EventLoop.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>

class EventLoopThread{
private:
    std::unique_ptr<EventLoop> _loop; // 当前线程绑定对应loop，自己管理
    std::unique_ptr<std::thread> _thread; // 工作线程
    std::mutex _mutex; // 锁资源，保证同步
    std::condition_variable _cond;
    std::atomic<bool> _started; // 是否启动

private:
    // 工作线程入口：创建工作线程，绑定loop，唤醒主线程
    void ThreadFunc();
    
public:
    EventLoopThread();

    // 返回线程当前绑定的loop
    EventLoop* StartLoop();
    
    ~EventLoopThread();
};
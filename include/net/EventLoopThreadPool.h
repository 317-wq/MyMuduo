#pragma once

/*
    创建并管理多个EventLoopThread线程对象
    保存管理所有work-loop
    轮询分发事件
*/

#include "net/EventLoopThread.h"
#include "net/EventLoop.h"
#include <vector>
#include <memory>

class EventLoopThreadPool{
private:
    EventLoop* _base_loop; // 主线程loop
    size_t _thread_num; // 线程数量
    std::vector<std::unique_ptr<EventLoopThread>> _threads; // 管理线程对象
    std::vector<EventLoop*> _loops; // 保存work-thread的loop
    size_t _next; // 轮询位置

public:
    EventLoopThreadPool(EventLoop* base_loop, size_t thread_num);
    
    // 启动线程池
    void Start();

    // 获取下一个loop，用于轮询发布任务
    EventLoop* GetNextLoop();
    
    ~EventLoopThreadPool();
};
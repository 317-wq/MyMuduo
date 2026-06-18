#include "net/EventLoopThreadPool.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *base_loop, size_t thread_num)
    :_base_loop(base_loop)
    ,_thread_num(thread_num)
    ,_next(0)
    {}

// 启动线程池，将thread_num个工作线程存储到_threads中备用
void EventLoopThreadPool::Start(){
    for(int i = 0; i < _thread_num; ++i){
        // std::unique_ptr<EventLoopThread> work_thread = std::make_unique<EventLoopThread>();
        auto work_thread = std::make_unique<EventLoopThread>();
        // 将loop设置进去并且返回对应的loop交付存储
        EventLoop *loop = work_thread->StartLoop();
        _loops.push_back(loop);
        // 临时对象转移资源
        _threads.push_back(std::move(work_thread));
    }
}

// 获取下一个loop，用于轮询发布任务
EventLoop *EventLoopThreadPool::GetNextLoop(){
    if(_loops.empty())
        return _base_loop;
    
    EventLoop* loop = _loops[_next];
    _next++;
    _next %= _loops.size();
    return loop;
}

EventLoopThreadPool::~EventLoopThreadPool() = default;
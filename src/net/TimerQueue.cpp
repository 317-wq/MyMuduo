#include "net/TimerQueue.h"
#include "base/Exception.h"

// 创建Timerfd
int TimerQueue::CreateTimerFd(){
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
        throw mymuduo::Exception::FromErrno("timerfd_create failed");
    return fd;
}

// 升序
bool TimerQueue::Compare::operator()(
        const TimerQueue::TimerPtr &t1, 
        const TimerQueue::TimerPtr &t2) const{
    return t1->ExpireTimePoint() < t2->ExpireTimePoint();
};

// 绑定Timer的超时事件回调，处理所有超时事件
void TimerQueue::HandleRead(){
    // 清除_timerfd的可读状态
    uint64_t cnt;
    if(read(_timerfd, &cnt, sizeof(cnt)) <= 0){
        return;
    }

    auto now = Timer::Clock::now(); // 当前时间
    std::vector<TimerPtr> expired; // 存放超时的定时器

    // 按照超时时间点来升序的，所以不需要++it，erase会处理，贪心
    for(auto it = _timers.begin(); it != _timers.end(); ){
        if((*it)->ExpireTimePoint() <= now){
            // 超时
            expired.push_back(*it);
            it = _timers.erase(it); // 迭代器失效
        }
        else{
            break;
        }
    }

    // 处理对应定时器的任务
    for(auto &timer : expired){
        timer->Run(); // 执行回调函数
        // 周期定时器
        if(timer->Repeat()){
            timer->Restart(); // 重置到期时间点
            _timers.insert(timer);
        }
    }

    // 再次更新最近超时时间
    ResetTimerFd();
}

// 更新最近超时时间，每次有新的定时器加入，最近的那个定时器作为读事件的触发条件
// 就是timerfd的下一次超时时间
void TimerQueue::ResetTimerFd(){
    if(_timers.empty()){
        return;
    }

    // 最近超时计时器
    TimerPtr timer = *(_timers.begin());
    auto now = Timer::Clock::now();
    auto diff = timer->ExpireTimePoint() - now;

    // 如果diff时间差值太小就会导致后面的ms.count()=0，就会导致it_val=0，关闭定时器
    // timer_fd的读事件就不会触发
    if(diff < std::chrono::milliseconds(1)){
        diff = std::chrono::milliseconds(1);
    }

    // 转化成ms
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff);

    itimerspec val;
    memset(&val, 0, sizeof(val));
    val.it_value.tv_sec = ms.count() / 1000; // 秒
    val.it_value.tv_nsec = (ms.count() % 1000) * 1000000; // 纳秒

    // 设置触发事件的时间
    timerfd_settime(_timerfd, 0, &val, nullptr);
}

TimerQueue::TimerQueue(EventLoop *loop)
    : _loop(loop)
    , _timerfd(CreateTimerFd())
    , _channel(std::make_unique<Channel>(_loop, _timerfd))
{
    _channel->SetReadCallback(std::bind(&TimerQueue::HandleRead, this));
    // 注册读事件
    _channel->EnableRead();
}

// 添加定时器
void TimerQueue::AddTimer(TimerPtr timer){
    _timers.insert(timer);
    // 更新timerfd的最近超时时间
    ResetTimerFd();
}

// 只负责将对应的定时器任务添加管理，刷新时间什么啊，都是handread处理的
// 一次性任务[sec秒之后执行]
void TimerQueue::RunAfter(int sec, Timer::Callback cb){
    auto expired = Timer::Clock::now() + std::chrono::seconds(sec);
    auto timer = std::make_shared<Timer>(expired, cb);
    AddTimer(timer);
}

// 周期性任务,repeat为true
void TimerQueue::RunEvery(int sec, Timer::Callback cb){
    auto interval = std::chrono::seconds(sec);
    auto expired = Timer::Clock::now() + interval;
    auto timer = std::make_shared<Timer>(expired, cb, true, interval);
    AddTimer(timer);
}

TimerQueue::~TimerQueue(){
    if(_channel){
        _channel->Remove();
    }
    if(_timerfd >= 0){
        close(_timerfd);
        _timerfd = -1;
    }
}
#include "../include/TimeWheel.h"

// 回调设置的函数，指针向前1每次，到哪里哪里就释放[删除连接等操作]
void TimeWheel::Tick(){
    _index = (_index + 1) % _capacity;

    // 先持锁收集超时 fd，释放锁后再回调，避免回调中调用 Remove 导致死锁
    std::vector<int> timeout_fds;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto &bucket = _wheel[_index];
        for (auto fd : bucket){
            timeout_fds.push_back(fd);
            _fd_slot_map.erase(fd); // fd与槽的映射表里面删除
        }
        bucket.clear(); // 直接一次性清空
    }

    // 无锁情况下执行回调
    for (auto fd : timeout_fds){
        if (_timeout_cb){
            _timeout_cb(fd);
        }
    }
}

TimeWheel::TimeWheel(EventLoop *loop, size_t timeout)
    :_loop(loop)
    ,_capacity(timeout)
    ,_wheel(_capacity)
    ,_index(0)
    ,_alive(std::make_shared<bool>(true))
{
    // 只要启动这个时间轮，就一直向后tick，就释放连接
    // 捕获 _alive 的拷贝，确保即使 TimeWheel 析构，Tick 也能安全检查
    auto alive = _alive;
    _loop->RunEvery(1, [this, alive]{
        if (*alive) Tick();
    });
}

// 设置超时回调函数
void TimeWheel::SetTimeoutCallback(TimeoutCallback cb){
    _timeout_cb = std::move(cb);
}

// 将fd插入到时间轮的最后位置[环形数组]
void TimeWheel::Insert(int fd){
    std::lock_guard<std::mutex> lock(_mutex);
    // 最后位置
    int slot = (_index + _capacity - 1) % _wheel.size();
    _wheel[slot].insert(fd);
    _fd_slot_map[fd] = slot;
}

// 刷新对应fd的结束时间[连接]，避免错误释放
// 注意：持锁调用 Remove/Insert，内部分别加锁，需用递归锁或拆分为内部无锁版本
// 这里用内部实现避免递归锁开销
void TimeWheel::Refresh(int fd){
    std::lock_guard<std::mutex> lock(_mutex);
    // 先删除旧位置
    auto it = _fd_slot_map.find(fd);
    if (it != _fd_slot_map.end()){
        int old_slot = it->second;
        auto &bucket = _wheel[old_slot];
        bucket.erase(fd);
        _fd_slot_map.erase(it);
    }
    // 再插入到最后位置
    int new_slot = (_index + _capacity - 1) % _wheel.size();
    _wheel[new_slot].insert(fd);
    _fd_slot_map[fd] = new_slot;
}

// 删除对应连接[删除对应存储的fd]
void TimeWheel::Remove(int fd){
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _fd_slot_map.find(fd);
    if (it == _fd_slot_map.end()){
        return; // 不存在，无需删除
    }
    int slot = it->second;
    _fd_slot_map.erase(it);
    auto &bucket = _wheel[slot];
    bucket.erase(fd);
}

TimeWheel::~TimeWheel(){
    *_alive = false; // 阻止后续 Tick 回调访问已析构对象
}
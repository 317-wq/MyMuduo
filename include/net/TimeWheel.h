#pragma once

/*
    时间轮模块
    职责：
    1. 管理连接超时
    2. 每秒转动一次时间轮
    3. 检测超时连接
    4. 通知上层关闭连接
*/

#include "net/EventLoop.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>

class TimeWheel
{
public:
    using TimeoutCallback = std::function<void(int)>; // 超时回调

private:
    EventLoop* _loop;
    int _capacity; // 时间轮的大小，其实就是按照超时时间来的
    std::vector<std::unordered_set<int>> _wheel; // 时间轮的本质上就是一个环形数组
    TimeoutCallback _timeout_cb; // 超时回调函数
    int _index; // 所处下标位置
    std::unordered_map<int, int> _fd_slot_map; // fd与槽位置的映射
    std::mutex _mutex; // 保护 _wheel 和 _fd_slot_map 的跨线程访问
    std::shared_ptr<bool> _alive; // 生命周期标志，析构后置 false，防止悬空回调

private:
    // 回调设置的函数，指针向前1每次，到哪里哪里就释放[删除连接等操作]
    void Tick();

public:
    TimeWheel(EventLoop* loop, size_t timeout);

    // 设置超时回调函数
    void SetTimeoutCallback(TimeoutCallback cb);

    // 将fd插入到时间轮的最后位置[环形数组]
    void Insert(int fd);

    // 刷新对应fd的结束时间[连接]，避免错误释放
    void Refresh(int fd);

    // 删除对应连接[删除对应存储的fd]
    void Remove(int fd);

    ~TimeWheel();
};
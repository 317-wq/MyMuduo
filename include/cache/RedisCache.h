#pragma once
#include "base/NoCopy.h"
#include "cache/RedisPool.h"
#include "net/EventLoop.h"
#include <functional>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

class RedisCache : NoCopy {
public:
    using RedisTask = std::function<void(redisContext*)>;
    using Callback = std::function<void()>;

    RedisCache(const std::string& host, int port, int pool_size = 4);
    ~RedisCache();
    void Execute(EventLoop* loop, RedisTask fn, Callback callback);

private:
    void WorkerThread();
    RedisPool _pool;
    struct Task { EventLoop* loop; RedisTask fn; Callback callback; };
    std::queue<Task> _tasks;
    std::mutex _task_mutex;
    std::condition_variable _task_cv;
    std::vector<std::thread> _workers;
    std::atomic<bool> _running{true};
};

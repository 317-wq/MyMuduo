#pragma once

#include "base/NoCopy.h"
#include "net/EventLoop.h"

#include <sw/redis++/redis++.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// RedisCache 负责异步执行 Redis 操作，并将回调切回 EventLoop 线程。
// Redis 对象内部使用 redis++ 自带的连接池，不再额外维护连接池。
class RedisCache : NoCopy {
public:
    using RedisTask = std::function<void(sw::redis::Redis*)>;
    using Callback = std::function<void()>;

    // 创建 Redis 客户端，并按照指定大小启动后台 worker。
    RedisCache(const std::string& host, int port, int pool_size = 4);

    // 停止 worker，等待已提交的任务执行完成。
    ~RedisCache();

    // 将 Redis 操作投递到后台线程，并在完成后执行回调。
    void Execute(EventLoop* loop, RedisTask fn, Callback callback);

private:
    // 描述一个待执行的 Redis 任务及其完成回调。
    struct Task {
        EventLoop* loop;
        RedisTask fn;
        Callback callback;
    };

    // 从任务队列取出任务并执行 Redis 操作。
    void WorkerThread();

    // 使用 redis++ 内置连接池的共享 Redis 客户端。
    std::unique_ptr<sw::redis::Redis> _redis;

    // 保存尚未被 worker 取走的任务。
    std::queue<Task> _tasks;

    // 保护任务队列的互斥锁。
    std::mutex _task_mutex;

    // 唤醒等待任务的 worker。
    std::condition_variable _task_cv;

    // Redis 后台任务执行线程。
    std::vector<std::thread> _workers;

    // 控制 worker 是否继续运行。
    std::atomic<bool> _running{true};
};

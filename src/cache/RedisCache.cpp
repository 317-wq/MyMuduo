#include "cache/RedisCache.h"

RedisCache::RedisCache(const std::string& host, int port, int pool_size)
    : _pool(host, port, pool_size)
{
    for (int i = 0; i < pool_size; ++i) {
        _workers.emplace_back(&RedisCache::WorkerThread, this);
    }
}

RedisCache::~RedisCache()
{
    _running = false;
    // 先关闭连接池，唤醒所有在 Borrow 上等待的 worker
    _pool.Close();
    _task_cv.notify_all();

    for (auto& t : _workers) {
        if (t.joinable())
            t.join();
    }
}

void RedisCache::Execute(EventLoop* loop, RedisTask fn, Callback callback)
{
    {
        std::lock_guard<std::mutex> lock(_task_mutex);
        _tasks.push({loop, std::move(fn), std::move(callback)});
    }
    _task_cv.notify_one();
}

void RedisCache::WorkerThread()
{
    while (_running) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(_task_mutex);
            _task_cv.wait(lock, [this] {
                return !_tasks.empty() || !_running;
            });

            if (!_running && _tasks.empty())
                break;

            task = std::move(_tasks.front());
            _tasks.pop();
        }

        // 从连接池借一个连接
        redisContext* ctx = _pool.Borrow(5000);
        if (!ctx) {
            // 池已关闭或超时，跳过此任务
            if (task.callback) {
                if (task.loop) {
                    task.loop->QueueInLoop(std::move(task.callback));
                } else {
                    task.callback();
                }
            }
            continue;
        }

        // 执行 Redis 任务
        bool ctx_broken = false;
        try {
            task.fn(ctx);
        } catch (...) {
            ctx_broken = true;
        }

        // 归还或剔除连接
        if (ctx_broken) {
            _pool.Remove(ctx);
        } else {
            _pool.Return(ctx);
        }

        // 将回调投递回 EventLoop 线程（loop 为 nullptr 时直接执行）
        if (task.callback) {
            if (task.loop) {
                task.loop->QueueInLoop(std::move(task.callback));
            } else {
                task.callback();  // 同步模式：直接在 Redis worker 线程执行
            }
        }
    }
}

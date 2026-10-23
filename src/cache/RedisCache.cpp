#include "cache/RedisCache.h"

RedisCache::RedisCache(const std::string& host, int port, int pool_size) {
    // 配置 redis++ 内置连接池，避免项目重复实现连接的创建、借出和回收。
    sw::redis::ConnectionOptions connection_options; // 定义单个连接的建立
    connection_options.host = host;
    connection_options.port = port;

    sw::redis::ConnectionPoolOptions pool_options; // 定义连接池的的属性
    pool_options.size = static_cast<std::size_t>(pool_size);

    _redis = std::make_unique<sw::redis::Redis>(connection_options, pool_options);

    // 每个 worker 从 redis++ 连接池获取连接执行一个任务。
    for (int i = 0; i < pool_size; ++i) {
        // 把一个绑定了worhthread的线程投放到池子里面
        _workers.emplace_back(&RedisCache::WorkerThread, this);
    }
}

RedisCache::~RedisCache() {
    // 先停止接收任务并唤醒等待中的 worker。
    _running = false;
    _task_cv.notify_all();

    for (auto& worker : _workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void RedisCache::Execute(EventLoop* loop, RedisTask fn, Callback callback) {
    {
        std::lock_guard<std::mutex> lock(_task_mutex);
        _tasks.push({loop, std::move(fn), std::move(callback)});
    }

    _task_cv.notify_one();
}

// 
void RedisCache::WorkerThread() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(_task_mutex);
            _task_cv.wait(lock, [this] {
                return !_tasks.empty() || !_running;
            });

            // 停止后仍然执行队列中的任务，避免析构时丢失已提交任务。
            if (!_running && _tasks.empty()) {
                break;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
        }

        try {
            // redis++ 会从内部连接池中选择可用连接。
            task.fn(_redis.get()); // 获取一个连接[里面去执行redis操作]
        } catch (const std::exception&) {
            // Redis 异常不应终止 worker；失败结果由业务回调自行处理。
        }

        // Redis 操作完成后，将回调投递回 EventLoop 线程。
        if (task.callback) {
            if (task.loop) {
                task.loop->QueueInLoop(std::move(task.callback));
            } else {
                task.callback();
            }
        }
    }
}

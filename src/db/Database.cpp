#include "db/Database.h"

#include <chrono>

Database::Database(const std::string& host, int port,
                   const std::string& user, const std::string& pass,
                   const std::string& dbname, int pool_size)
    : _pool(host, port, user, pass, dbname, pool_size)
{
    for (int i = 0; i < pool_size; ++i) {
        _workers.emplace_back(&Database::WorkerThread, this);
    }
}

Database::~Database()
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

void Database::Execute(EventLoop* loop, DBTask fn, Callback callback)
{
    {
        std::lock_guard<std::mutex> lock(_task_mutex);
        _tasks.push({loop, std::move(fn), std::move(callback)});
    }
    _task_cv.notify_one();
}

void Database::WorkerThread()
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
        sql::Connection* conn = nullptr;
        int retries = 3;
        while (retries-- > 0 && _running) {
            conn = _pool.Borrow(5000);
            if (conn) break;
        }

        if (!conn) {
            // 池已关闭或多次重试后仍无法获取连接
            // 仍然执行 callback 以唤醒 RunDBSync，但 fn 未执行
            if (task.callback) {
                if (task.loop) {
                    task.loop->QueueInLoop(std::move(task.callback));
                } else {
                    task.callback();
                }
            }
            continue;
        }

        // 执行 DB 任务
        bool conn_broken = false;
        try {
            task.fn(conn);
        } catch (...) {
            conn_broken = true;
        }

        // 归还或剔除连接
        if (conn_broken) {
            _pool.Remove(conn);
        } else {
            _pool.Return(conn);
        }

        // 将回调投递回 EventLoop 线程（loop 为 nullptr 时直接执行）
        if (task.callback) {
            if (task.loop) {
                task.loop->QueueInLoop(std::move(task.callback));
            } else {
                task.callback();  // 同步模式：直接在 DB worker 线程执行
            }
        }
    }
}

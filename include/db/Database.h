#pragma once

/*
    数据库异步执行层

    职责：
    1. 持有 ConnectionPool，多个 worker 线程共享连接
    2. 每个任务从池中 Borrow 连接 → 执行 → Return 连接
    3. 连接失效时自动剔除，下次 Borrow 时创建新连接补充
    4. 结果通过回调投递回指定的 EventLoop，不阻塞 IO 线程

    典型用法：
        Database db(host, port, user, pass, dbname, 4);

        db.Execute(io_loop,
            [&](sql::Connection* conn) {
                // 这段代码在 DB 线程执行，可以阻塞
                UserDao::InsertUser(conn, email, hash, salt, name, out_id);
            },
            [&, out_id]() {
                // 这段代码回到 io_loop 执行
                SendRegisterResponse(conn, out_id);
            });
*/

#include "base/NoCopy.h"
#include "db/ConnectionPool.h"
#include "net/EventLoop.h"

#include <memory>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <atomic>

class Database : NoCopy {
public:
    using DBTask = std::function<void(sql::Connection*)>;
    using Callback = std::function<void()>;

    // host/port/user/pass/dbname: MySQL 连接参数
    // pool_size: 连接池最大连接数（同时也是 worker 线程数）
    Database(const std::string& host, int port,
             const std::string& user, const std::string& pass,
             const std::string& dbname, int pool_size = 4);
    ~Database();

    // 异步执行 DB 任务
    // - fn: 在 DB worker 线程执行（可阻塞）
    // - callback: 在 loop 指定的 EventLoop 线程执行
    void Execute(EventLoop* loop, DBTask fn, Callback callback);

private:
    void WorkerThread();

    // 连接池
    ConnectionPool _pool;

    // 任务队列
    struct Task {
        EventLoop* loop;
        DBTask     fn;
        Callback   callback;
    };
    std::queue<Task> _tasks;
    std::mutex _task_mutex;
    std::condition_variable _task_cv;

    // worker 线程
    std::vector<std::thread> _workers;
    std::atomic<bool> _running{true};
};

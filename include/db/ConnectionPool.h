#pragma once

/*
    简单 MySQL 连接池

    设计要点：
    1. 固定最大连接数，按需创建（懒初始化）
    2. Borrow / Return 语义，线程安全
    3. Borrow 时自动检测连接健康状态，剔除坏连接
    4. 所有连接繁忙时阻塞等待（支持超时）

    典型用法：
        ConnectionPool pool(host, port, user, pass, dbname, 8);
        sql::Connection* conn = pool.Borrow(5000);
        if (conn) {
            // ... 执行 SQL ...
            pool.Return(conn);
        }
*/

#include "base/NoCopy.h"

#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace sql {
    class Connection;
}

class ConnectionPool : NoCopy {
public:
    // host/port/user/pass/dbname: MySQL 连接参数
    // max_size: 最大连接数
    ConnectionPool(const std::string& host, int port,
                   const std::string& user, const std::string& pass,
                   const std::string& dbname, int max_size = 8);
    ~ConnectionPool();

    // 借用一个连接（阻塞等待，直到有可用连接或超时）
    // timeout_ms: 最长等待毫秒数，0 表示无限等待
    // 返回 nullptr 表示超时或池已关闭
    sql::Connection* Borrow(int timeout_ms = 5000);

    // 归还连接
    void Return(sql::Connection* conn);

    // 剔除坏连接（任务执行中发现连接失效时调用）
    // 池会自动在下一次 Borrow 时创建新连接来补充
    void Remove(sql::Connection* conn);

    // 当前池中连接总数（空闲 + 借出中）
    int ActiveCount() const { return _active_count.load(); }

    // 空闲连接数
    int IdleCount();

    // 关闭连接池（唤醒所有等待者，Borrow 返回 nullptr）
    void Close();

private:
    sql::Connection* CreateConnection();
    bool CheckHealth(sql::Connection* conn);

    std::string _host;
    int         _port;
    std::string _user;
    std::string _pass;
    std::string _dbname;
    int         _max_size;

    std::queue<sql::Connection*> _pool;      // 空闲连接队列
    std::mutex                    _mutex;
    std::condition_variable       _cv;

    std::atomic<int> _active_count{0};       // 总连接数（空闲 + 借出）
    std::atomic<bool> _closed{false};        // 池已关闭
};

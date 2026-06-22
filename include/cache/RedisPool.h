#pragma once

/*
    hiredis 连接池

    设计要点：
    1. 固定最大连接数，按需创建（懒初始化）
    2. Borrow / Return 语义，线程安全
    3. Borrow 时自动检测连接健康状态（PING），剔除坏连接
    4. 所有连接繁忙时阻塞等待（支持超时）

    典型用法：
        RedisPool pool("127.0.0.1", 6379, 8);
        redisContext* ctx = pool.Borrow(5000);
        if (ctx) {
            redisReply* r = (redisReply*)redisCommand(ctx, "GET key");
            if (r) { freeReplyObject(r); }
            pool.Return(ctx);
        }
*/

#include "base/NoCopy.h"

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <hiredis/hiredis.h>
}

class RedisPool : NoCopy {
public:
    // host/port: Redis 服务器地址
    // max_size: 最大连接数
    explicit RedisPool(const std::string& host, int port, int max_size = 8);
    ~RedisPool();

    // 借用一个连接（阻塞等待，直到有可用连接或超时）
    // timeout_ms: 最长等待毫秒数，0 表示无限等待
    // 返回 nullptr 表示超时或池已关闭
    redisContext* Borrow(int timeout_ms = 5000);

    // 归还连接
    void Return(redisContext* ctx);

    // 剔除坏连接（任务执行中发现连接失效时调用）
    // 池会自动在下一次 Borrow 时创建新连接来补充
    void Remove(redisContext* ctx);

    // 当前池中连接总数（空闲 + 借出中）
    int ActiveCount() const { return _active_count.load(); }

    // 空闲连接数
    int IdleCount();

    // 关闭连接池（唤醒所有等待者，Borrow 返回 nullptr）
    void Close();

private:
    redisContext* CreateConnection();
    bool CheckHealth(redisContext* ctx);

    std::string _host;
    int         _port;
    int         _max_size;

    std::queue<redisContext*> _pool;      // 空闲连接队列
    std::mutex                _mutex;
    std::condition_variable   _cv;

    std::atomic<int> _active_count{0};       // 总连接数（空闲 + 借出）
    std::atomic<bool> _closed{false};        // 池已关闭
};

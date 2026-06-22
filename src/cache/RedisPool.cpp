#include "cache/RedisPool.h"

#include <chrono>

RedisPool::RedisPool(const std::string& host, int port, int max_size)
    : _host(host), _port(port), _max_size(max_size)
{
}

RedisPool::~RedisPool()
{
    _closed = true;
    _cv.notify_all();

    std::lock_guard<std::mutex> lock(_mutex);
    while (!_pool.empty()) {
        redisContext* ctx = _pool.front();
        _pool.pop();
        redisFree(ctx);
        _active_count--;
    }
}

redisContext* RedisPool::CreateConnection()
{
    struct timeval tv;
    tv.tv_sec = 2;   // 连接超时 2 秒
    tv.tv_usec = 0;

    redisContext* ctx = redisConnectWithTimeout(_host.c_str(), _port, tv);
    if (ctx == nullptr || ctx->err) {
        if (ctx) {
            redisFree(ctx);
        }
        return nullptr;
    }

    // 启用 TCP_NODELAY（降低延迟）
    // hiredis 默认已启用，无需额外设置

    return ctx;
}

bool RedisPool::CheckHealth(redisContext* ctx)
{
    if (!ctx) return false;

    // 检查连接是否已断开
    if (ctx->err) return false;

    // 发送 PING
    redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
    if (!reply) return false;

    bool healthy = (reply->type == REDIS_REPLY_STATUS &&
                    std::string(reply->str, reply->len) == "PONG");
    freeReplyObject(reply);
    return healthy;
}

redisContext* RedisPool::Borrow(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(_mutex);

    while (!_closed) {
        // 1. 先从空闲队列取一个连接
        if (!_pool.empty()) {
            redisContext* ctx = _pool.front();
            _pool.pop();

            // 检查健康状态
            if (CheckHealth(ctx)) {
                return ctx;  // 健康连接，直接返回
            }

            // 连接已失效，销毁并继续尝试
            redisFree(ctx);
            _active_count--;
            continue;
        }

        // 2. 空闲队列为空，但还可以创建新连接
        if (_active_count < _max_size) {
            redisContext* ctx = CreateConnection();
            if (ctx) {
                _active_count++;
                return ctx;
            }
            // 创建失败（Redis 可能挂了），返回 nullptr 让调用者重试
            return nullptr;
        }

        // 3. 连接数已达上限，等待归还
        if (timeout_ms > 0) {
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeout_ms);
            if (_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                return nullptr;  // 超时
            }
        } else {
            _cv.wait(lock);  // 无限等待
        }
    }

    return nullptr;  // 池已关闭
}

void RedisPool::Return(redisContext* ctx)
{
    if (!ctx) return;

    std::lock_guard<std::mutex> lock(_mutex);

    // 简单检查：连接已出错则丢弃
    if (ctx->err) {
        redisFree(ctx);
        _active_count--;
    } else {
        _pool.push(ctx);
    }

    _cv.notify_one();
}

void RedisPool::Remove(redisContext* ctx)
{
    if (!ctx) return;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active_count--;
    }

    redisFree(ctx);
    _cv.notify_one();
}

void RedisPool::Close()
{
    _closed = true;
    _cv.notify_all();
}

int RedisPool::IdleCount()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<int>(_pool.size());
}

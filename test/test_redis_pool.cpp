/*
    RedisPool 连接池单元测试

    覆盖：
    - 基本 Borrow/Return
    - 健康检查（PING）
    - 并发安全
    - 超时处理
    - 池关闭
    - 坏连接剔除
*/

#include "cache/RedisPool.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

// 测试 fixture：确保 Redis 可用
class RedisPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 验证 Redis 可用
        redisContext* ctx = redisConnect("127.0.0.1", 6379);
        if (ctx == nullptr || ctx->err) {
            if (ctx) redisFree(ctx);
            GTEST_SKIP() << "Redis server is not running, skipping tests";
        }
        redisFree(ctx);
    }
};

TEST_F(RedisPoolTest, CreateAndBorrow) {
    RedisPool pool("127.0.0.1", 6379, 4);

    redisContext* ctx = pool.Borrow(1000);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->err, 0);
    EXPECT_EQ(pool.ActiveCount(), 1);
    EXPECT_EQ(pool.IdleCount(), 0);

    pool.Return(ctx);
    EXPECT_EQ(pool.ActiveCount(), 1);
    EXPECT_EQ(pool.IdleCount(), 1);
}

TEST_F(RedisPoolTest, PingHealthCheck) {
    RedisPool pool("127.0.0.1", 6379, 4);

    redisContext* ctx = pool.Borrow(1000);
    ASSERT_NE(ctx, nullptr);

    // 执行真实的 PING
    redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
    ASSERT_NE(reply, nullptr);
    EXPECT_EQ(reply->type, REDIS_REPLY_STATUS);
    EXPECT_STREQ(reply->str, "PONG");
    freeReplyObject(reply);

    pool.Return(ctx);
}

TEST_F(RedisPoolTest, ReuseConnection) {
    RedisPool pool("127.0.0.1", 6379, 4);

    // 第一次借用
    redisContext* ctx1 = pool.Borrow(1000);
    ASSERT_NE(ctx1, nullptr);
    pool.Return(ctx1);

    // 第二次借用应该是同一个连接（池中只有一个）
    redisContext* ctx2 = pool.Borrow(1000);
    ASSERT_NE(ctx2, nullptr);
    EXPECT_EQ(ctx1, ctx2);  // 同一个指针

    pool.Return(ctx2);
}

TEST_F(RedisPoolTest, MaxSizeLimit) {
    const int max = 3;
    RedisPool pool("127.0.0.1", 6379, max);

    // 借出所有连接
    std::vector<redisContext*> borrowed;
    for (int i = 0; i < max; ++i) {
        redisContext* ctx = pool.Borrow(500);
        ASSERT_NE(ctx, nullptr) << "Failed to borrow connection " << i;
        borrowed.push_back(ctx);
    }

    EXPECT_EQ(pool.ActiveCount(), max);
    EXPECT_EQ(pool.IdleCount(), 0);

    // 归还所有
    for (auto* ctx : borrowed) {
        pool.Return(ctx);
    }

    EXPECT_EQ(pool.ActiveCount(), max);
    EXPECT_EQ(pool.IdleCount(), max);
}

TEST_F(RedisPoolTest, BorrowTimeout) {
    const int max = 2;
    RedisPool pool("127.0.0.1", 6379, max);

    // 借出所有连接
    redisContext* ctx1 = pool.Borrow(500);
    redisContext* ctx2 = pool.Borrow(500);
    ASSERT_NE(ctx1, nullptr);
    ASSERT_NE(ctx2, nullptr);

    // 第三次借用应该超时（没有可用连接，也不归还）
    redisContext* ctx3 = pool.Borrow(200);  // 200ms 超时
    EXPECT_EQ(ctx3, nullptr);

    // 归还后可以再次借用
    pool.Return(ctx1);
    redisContext* ctx4 = pool.Borrow(500);
    ASSERT_NE(ctx4, nullptr);

    pool.Return(ctx2);
    pool.Return(ctx4);
}

TEST_F(RedisPoolTest, RemoveBrokenConnection) {
    const int max = 3;
    RedisPool pool("127.0.0.1", 6379, max);

    redisContext* ctx = pool.Borrow(1000);
    ASSERT_NE(ctx, nullptr);
    int before = pool.ActiveCount();

    // 剔除坏连接
    pool.Remove(ctx);
    EXPECT_EQ(pool.ActiveCount(), before - 1);

    // 下次 Borrow 应该创建新连接
    redisContext* ctx2 = pool.Borrow(1000);
    ASSERT_NE(ctx2, nullptr);
    EXPECT_EQ(pool.ActiveCount(), before);  // 恢复到原来的数量
    pool.Return(ctx2);
}

TEST_F(RedisPoolTest, ClosePool) {
    RedisPool pool("127.0.0.1", 6379, 4);

    // 先借一个连接验证池正常
    redisContext* ctx = pool.Borrow(1000);
    ASSERT_NE(ctx, nullptr);
    pool.Return(ctx);

    // 关闭池
    pool.Close();

    // 关闭后 Borrow 应返回 nullptr
    redisContext* ctx2 = pool.Borrow(100);
    EXPECT_EQ(ctx2, nullptr);
}

TEST_F(RedisPoolTest, ConcurrentBorrowReturn) {
    const int max = 4;
    const int thread_count = 8;
    const int ops_per_thread = 50;
    RedisPool pool("127.0.0.1", 6379, max);

    std::atomic<int> success_count{0};
    std::atomic<int> timeout_count{0};

    auto worker = [&]() {
        for (int i = 0; i < ops_per_thread; ++i) {
            redisContext* ctx = pool.Borrow(5000);
            if (ctx) {
                // 执行一个简单的命令验证连接有效
                redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
                if (reply && reply->type == REDIS_REPLY_STATUS) {
                    success_count++;
                }
                if (reply) freeReplyObject(reply);
                pool.Return(ctx);
            } else {
                timeout_count++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // 所有操作都应该成功（连接池足以及时归还）
    EXPECT_GT(success_count, 0);
    EXPECT_EQ(timeout_count, 0);
    EXPECT_EQ(pool.ActiveCount(), max);  // 创建了 max 个连接
}

TEST_F(RedisPoolTest, BorrowAfterRemoveCreatesNew) {
    RedisPool pool("127.0.0.1", 6379, 3);

    // 借出全部 3 个连接
    redisContext* ctx1 = pool.Borrow(1000);
    redisContext* ctx2 = pool.Borrow(1000);
    redisContext* ctx3 = pool.Borrow(1000);
    ASSERT_NE(ctx1, nullptr);
    ASSERT_NE(ctx2, nullptr);
    ASSERT_NE(ctx3, nullptr);
    EXPECT_EQ(pool.ActiveCount(), 3);

    // Remove ctx1（模拟坏连接剔除）
    pool.Remove(ctx1);
    EXPECT_EQ(pool.ActiveCount(), 2);  // 3 - 1

    // 不归还任何连接，直接 Borrow — 应有空间创建新连接
    redisContext* ctx4 = pool.Borrow(1000);
    ASSERT_NE(ctx4, nullptr);
    EXPECT_NE(ctx4, ctx2);  // 是新创建的连接，不是归还的
    EXPECT_NE(ctx4, ctx3);
    EXPECT_EQ(pool.ActiveCount(), 3);  // 恢复到 max_size

    // 归还所有
    pool.Return(ctx2);
    pool.Return(ctx3);
    pool.Return(ctx4);
}

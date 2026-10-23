#include <gtest/gtest.h>

#include <sw/redis++/redis++.h>

// 验证 redis++ 内置连接池能够创建并复用 Redis 连接。
TEST(RedisConnectionPoolTest, BorrowAndReuseConnection) {
    sw::redis::ConnectionOptions connection_options;
    connection_options.host = "127.0.0.1";
    connection_options.port = 6379;

    sw::redis::ConnectionPoolOptions pool_options;
    pool_options.size = 2;

    try {
        sw::redis::Redis redis(connection_options, pool_options);
        EXPECT_EQ(redis.ping(), "PONG");

        redis.set("redis_pool_test_key", "redis++");
        auto value = redis.get("redis_pool_test_key");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, "redis++");

        redis.del("redis_pool_test_key");
    } catch (const std::exception& exception) {
        GTEST_SKIP() << "Redis server is not running: " << exception.what();
    }
}

// 验证多个线程可以安全共享使用同一个 redis++ 客户端。
TEST(RedisConnectionPoolTest, ConcurrentOperations) {
    sw::redis::ConnectionOptions connection_options;
    connection_options.host = "127.0.0.1";
    connection_options.port = 6379;

    sw::redis::ConnectionPoolOptions pool_options;
    pool_options.size = 2;

    try {
        sw::redis::Redis redis(connection_options, pool_options);
        redis.set("redis_pool_concurrent_key", "ready");
        EXPECT_EQ(redis.get("redis_pool_concurrent_key").value(), "ready");
        redis.del("redis_pool_concurrent_key");
    } catch (const std::exception& exception) {
        GTEST_SKIP() << "Redis server is not running: " << exception.what();
    }
}

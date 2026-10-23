#include "cache/RedisCache.h"
#include "net/EventLoop.h"
#include <gtest/gtest.h>
#include <sw/redis++/redis++.h>
#include <chrono>
#include <memory>
#include <thread>
#include <unordered_map>
#include <iterator>

class RedisCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            sw::redis::Redis r(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            r.ping();
        } catch (const std::exception&) { GTEST_SKIP() << "Redis server is not running"; }
        cache = std::make_unique<RedisCache>("127.0.0.1", 6379, 4);
    }
    void Wait(EventLoop& loop) {
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            loop.DoPendingFunctors();
        }
    }
    std::unique_ptr<RedisCache> cache;
};

TEST_F(RedisCacheTest, SetGetDelete) {
    EventLoop loop;
    bool done = false;
    cache->Execute(&loop, [](sw::redis::Redis* r) { r->set("test_key", "hello"); }, [&] { done = true; });
    Wait(loop); EXPECT_TRUE(done);
    std::string value;
    cache->Execute(&loop, [&](sw::redis::Redis* r) { auto v = r->get("test_key"); if (v) value = *v; }, [&] { done = true; });
    Wait(loop); EXPECT_EQ(value, "hello");
    long long deleted = 0;
    cache->Execute(&loop, [&](sw::redis::Redis* r) { deleted = r->del("test_key"); }, nullptr);
    Wait(loop); EXPECT_EQ(deleted, 1);
}

TEST_F(RedisCacheTest, ExpireTTL) {
    EventLoop loop;
    long long ttl = -1;
    cache->Execute(&loop, [](sw::redis::Redis* r) { r->setex("test_ttl", 5, "value"); }, nullptr);
    Wait(loop);
    cache->Execute(&loop, [&](sw::redis::Redis* r) { ttl = r->command<long long>("TTL", "test_ttl"); }, nullptr);
    Wait(loop); EXPECT_GT(ttl, 0); EXPECT_LE(ttl, 5);
    cache->Execute(&loop, [](sw::redis::Redis* r) { r->del("test_ttl"); }, nullptr);
    Wait(loop);
}

TEST_F(RedisCacheTest, HashOperations) {
    EventLoop loop;
    cache->Execute(&loop, [](sw::redis::Redis* r) { r->hset("test_hash", "field1", "val1"); r->hset("test_hash", "field2", "val2"); }, nullptr);
    Wait(loop);
    std::string value;
    cache->Execute(&loop, [&](sw::redis::Redis* r) { auto v = r->hget("test_hash", "field1"); if (v) value = *v; }, nullptr);
    Wait(loop); EXPECT_EQ(value, "val1");
    std::unordered_map<std::string, std::string> values;
    cache->Execute(&loop, [&](sw::redis::Redis* r) { r->hgetall("test_hash", std::inserter(values, values.end())); }, nullptr);
    Wait(loop); EXPECT_EQ(values.size(), 2u);
    cache->Execute(&loop, [](sw::redis::Redis* r) { r->del("test_hash"); }, nullptr); Wait(loop);
}

TEST_F(RedisCacheTest, ConcurrentTasks) {
    EventLoop loop;
    int completed = 0;
    for (int i = 0; i < 10; ++i) {
        cache->Execute(&loop, [i](sw::redis::Redis* r) { r->set("cache_test_" + std::to_string(i), std::to_string(i)); }, [&] { ++completed; });
    }
    Wait(loop); EXPECT_EQ(completed, 10);
    for (int i = 0; i < 10; ++i) cache->Execute(&loop, [i](sw::redis::Redis* r) { r->del("cache_test_" + std::to_string(i)); }, nullptr);
    Wait(loop);
}

TEST_F(RedisCacheTest, PingAndNullCallback) {
    EventLoop loop;
    cache->Execute(&loop, [](sw::redis::Redis* r) { EXPECT_EQ(r->ping(), "PONG"); }, nullptr);
    Wait(loop); SUCCEED();
}

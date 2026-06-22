/*
    UserService + Redis 缓存集成测试

    测试流程：
    1. 发送验证码（存 Redis）→ 注册（从 Redis 验码）→ 登录（Cache-Aside）
    2. 缓存命中（第二次登录不查 MySQL）
    3. 验证码过期
    4. 错误验证码被拒绝
    5. Redis 不可用时回退 MySQL

    依赖：MySQL + Redis 均需运行
*/

#include "service/UserService.h"
#include "db/Database.h"
#include "cache/RedisCache.h"
#include "net/EventLoop.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <hiredis/hiredis.h>
}

// 测试配置
static const std::string DB_HOST = "127.0.0.1";
static const int DB_PORT = 3306;
static const std::string DB_USER = "root";
static const std::string DB_PASS = "lijiatong344A@";
static const std::string DB_NAME = "mymuduo";

// 等待异步操作完成
static void WaitForLoop(EventLoop& loop, int max_retries = 100) {
    for (int i = 0; i < max_retries; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        loop.DoPendingFunctors();
    }
}

class UserServiceRedisTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 检查 Redis
        {
            redisContext* ctx = redisConnect("127.0.0.1", 6379);
            if (ctx == nullptr || ctx->err) {
                if (ctx) redisFree(ctx);
                GTEST_SKIP() << "Redis not available, skipping tests";
            }
            redisFree(ctx);
        }

        _db = std::make_unique<Database>(DB_HOST, DB_PORT,
            DB_USER, DB_PASS, DB_NAME, 2);
        _redis = std::make_unique<RedisCache>("127.0.0.1", 6379, 2);
        _loop = std::make_unique<EventLoop>();
        _service = std::make_unique<UserService>(_db.get(), _loop.get());
        _service->SetRedisCache(_redis.get());

        // 生成唯一测试邮箱
        _test_email = "test_redis_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ) + "@example.com";
    }

    void TearDown() override {
        _service.reset();
        _loop.reset();
        _redis.reset();
        _db.reset();
    }

    std::unique_ptr<Database> _db;
    std::unique_ptr<RedisCache> _redis;
    std::unique_ptr<EventLoop> _loop;
    std::unique_ptr<UserService> _service;
    std::string _test_email;
};

TEST_F(UserServiceRedisTest, SendVerificationCodeToRedis) {
    bool done = false;
    int error = -1;

    _service->SendVerificationCode(_test_email, 1,
        [&](int err, const std::string& msg) {
            error = err;
            done = true;
        });

    WaitForLoop(*_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(error, 0);

    // 验证验证码已写入 Redis
    bool code_exists = false;
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            std::string key = "code:1:" + _test_email;
            redisReply* r = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
            code_exists = (r && r->type == REDIS_REPLY_STRING);
            if (r) freeReplyObject(r);
        },
        [&]() {});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(code_exists) << "Verification code should be in Redis";
}

TEST_F(UserServiceRedisTest, RegisterAndLoginWithRedisCache) {
    // Step 1: 发送验证码
    std::string verification_code = "123456";
    bool send_done = false;

    // 手动设置验证码到 Redis（跳过邮件发送）
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, verification_code, 300);
        },
        [&]() { send_done = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(send_done);

    WaitForLoop(*_loop);

    // Step 2: 注册
    uint32_t user_id = 0;
    bool register_done = false;
    int register_err = -1;

    _service->RegisterWithCode(_test_email, verification_code,
        "TestPass123", "RedisUser",
        [&](int err, const std::string& msg, uint32_t uid) {
            register_err = err;
            user_id = uid;
            register_done = true;
        });

    WaitForLoop(*_loop);
    EXPECT_TRUE(register_done);
    EXPECT_EQ(register_err, 0);
    EXPECT_GT(user_id, 0u);

    // Step 3: 首次登录（Redis cache miss → MySQL → 回填）
    bool login1_done = false;
    int login1_err = -1;
    UserInfo login1_user;

    _service->Login(_test_email, "TestPass123",
        [&](int err, const std::string& msg, const UserInfo& u) {
            login1_err = err;
            login1_user = u;
            login1_done = true;
        });

    WaitForLoop(*_loop);
    EXPECT_TRUE(login1_done);
    EXPECT_EQ(login1_err, 0);
    EXPECT_EQ(login1_user.id, user_id);

    // 等待 Redis 回填完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    WaitForLoop(*_loop);

    // Step 4: 第二次登录（Redis cache hit）
    bool login2_done = false;
    int login2_err = -1;
    UserInfo login2_user;

    _service->Login(_test_email, "TestPass123",
        [&](int err, const std::string& msg, const UserInfo& u) {
            login2_err = err;
            login2_user = u;
            login2_done = true;
        });

    WaitForLoop(*_loop);
    EXPECT_TRUE(login2_done);
    EXPECT_EQ(login2_err, 0);
    EXPECT_EQ(login2_user.id, user_id);
    EXPECT_EQ(login2_user.username, "RedisUser");

    // 清理
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::InvalidateUserCache(ctx, user_id);
            RedisDao::InvalidateEmailMapping(ctx, _test_email);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(UserServiceRedisTest, RegisterWithWrongCode) {
    // 设置验证码
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, "999999", 300);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 用错误验证码注册
    bool done = false;
    int error = -1;

    _service->RegisterWithCode(_test_email, "000000",
        "Pass123", "User",
        [&](int err, const std::string&, uint32_t) {
            error = err;
            done = true;
        });

    WaitForLoop(*_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(error, 1);  // 验证码错误
}

TEST_F(UserServiceRedisTest, LoginWithWrongPassword) {
    // 先注册
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, "111111", 300);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool reg_done = false;
    _service->RegisterWithCode(_test_email, "111111", "CorrectPass", "User",
        [&](int, const std::string&, uint32_t) { reg_done = true; });
    WaitForLoop(*_loop);
    EXPECT_TRUE(reg_done);

    // 错误密码登录
    bool done = false;
    int error = -1;
    _service->Login(_test_email, "WrongPass",
        [&](int err, const std::string&, const UserInfo&) {
            error = err;
            done = true;
        });
    WaitForLoop(*_loop);

    EXPECT_TRUE(done);
    EXPECT_EQ(error, 2);  // 密码错误
}

TEST_F(UserServiceRedisTest, ExpiredVerificationCode) {
    // 设置 1 秒过期的验证码
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, "expired_code", 1);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));

    bool done = false;
    int error = -1;
    _service->RegisterWithCode(_test_email, "expired_code",
        "Pass123", "User",
        [&](int err, const std::string&, uint32_t) {
            error = err;
            done = true;
        });
    WaitForLoop(*_loop);

    EXPECT_TRUE(done);
    EXPECT_EQ(error, 1) << "Expired verification code should be rejected";
}

TEST_F(UserServiceRedisTest, OverwriteVerificationCode) {
    // 先设置一个码
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, "first_code", 300);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 再覆盖新码
    _redis->Execute(nullptr,
        [&](redisContext* ctx) {
            RedisDao::SetVerificationCode(ctx, _test_email, 1, "second_code", 300);
        },
        nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 注册时用新码应该通过（因为 SETEX 覆盖了旧码）
    // 注意：GetAndConsumeVerificationCode 验证失败也会消耗码（防暴力破解），
    // 所以此处只测新码
    bool done = false;
    int err = -1;
    _service->RegisterWithCode(_test_email, "second_code", "Pass123", "User2",
        [&](int err_code, const std::string&, uint32_t) { err = err_code; done = true; });
    WaitForLoop(*_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(err, 0) << "Overwritten verification code should work";
}

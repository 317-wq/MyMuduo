/*
    RedisDao 数据访问层单元测试

    覆盖：
    - 用户信息缓存（CacheUserInfo / GetCachedUserInfo / InvalidateUserCache）
    - 邮箱映射（CacheEmailMapping / GetUserIdByEmail / InvalidateEmailMapping）
    - 验证码（SetVerificationCode / GetAndConsumeVerificationCode）
    - 在线状态（SetUserOnline / IsUserOnline / SetUserOffline）
    - TTL 过期、更新覆盖、边界条件
*/

#include "cache/RedisDao.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

extern "C" {
#include <hiredis/hiredis.h>
}

// 测试 fixture
class RedisDaoTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx = redisConnect("127.0.0.1", 6379);
        if (ctx == nullptr || ctx->err) {
            if (ctx) { redisFree(ctx); ctx = nullptr; }
            GTEST_SKIP() << "Redis server is not running, skipping tests";
        }
    }

    void TearDown() override {
        if (ctx) {
            redisFree(ctx);
            ctx = nullptr;
        }
    }

    UserInfo MakeTestUser() {
        UserInfo u;
        u.id = 99999;
        u.email = "test_redisdao@example.com";
        u.username = "TestRedisDaoUser";
        u.password = "deadbeef1234";
        u.salt = "abcdef";
        u.avatar = "/avatars/default.png";
        u.created_at = "2026-01-15 10:30:00";
        return u;
    }

    redisContext* ctx = nullptr;
};

// ============================================================
// 用户信息缓存
// ============================================================

TEST_F(RedisDaoTest, CacheAndGetUserInfo) {
    UserInfo user = MakeTestUser();

    // 写入缓存
    EXPECT_TRUE(RedisDao::CacheUserInfo(ctx, user));

    // 读取缓存
    UserInfo cached;
    EXPECT_TRUE(RedisDao::GetCachedUserInfo(ctx, user.id, cached));
    EXPECT_EQ(cached.id, user.id);
    EXPECT_EQ(cached.email, user.email);
    EXPECT_EQ(cached.username, user.username);
    EXPECT_EQ(cached.password, user.password);
    EXPECT_EQ(cached.salt, user.salt);
    EXPECT_EQ(cached.avatar, user.avatar);
    EXPECT_EQ(cached.created_at, user.created_at);

    // 清理
    RedisDao::InvalidateUserCache(ctx, user.id);
}

TEST_F(RedisDaoTest, InvalidateUserCache) {
    UserInfo user = MakeTestUser();
    RedisDao::CacheUserInfo(ctx, user);

    // 失效缓存
    EXPECT_TRUE(RedisDao::InvalidateUserCache(ctx, user.id));

    // 读取应失败
    UserInfo cached;
    EXPECT_FALSE(RedisDao::GetCachedUserInfo(ctx, user.id, cached));
}

TEST_F(RedisDaoTest, GetCachedUserInfoNotFound) {
    UserInfo cached;
    // 不存在的用户 ID
    EXPECT_FALSE(RedisDao::GetCachedUserInfo(ctx, 88888888, cached));
}

TEST_F(RedisDaoTest, CacheUserInfoOverwrite) {
    UserInfo user = MakeTestUser();
    RedisDao::CacheUserInfo(ctx, user);

    // 更新用户名和头像
    user.username = "UpdatedName";
    user.avatar = "/avatars/new.png";
    RedisDao::CacheUserInfo(ctx, user);

    // 读取应返回最新值
    UserInfo cached;
    EXPECT_TRUE(RedisDao::GetCachedUserInfo(ctx, user.id, cached));
    EXPECT_EQ(cached.username, "UpdatedName");
    EXPECT_EQ(cached.avatar, "/avatars/new.png");

    // 清理
    RedisDao::InvalidateUserCache(ctx, user.id);
}

// ============================================================
// 邮箱映射
// ============================================================

TEST_F(RedisDaoTest, CacheAndGetEmailMapping) {
    const std::string email = "mapping_test@example.com";
    uint32_t user_id = 42;

    EXPECT_TRUE(RedisDao::CacheEmailMapping(ctx, email, user_id));

    uint32_t out_id = 0;
    EXPECT_TRUE(RedisDao::GetUserIdByEmail(ctx, email, out_id));
    EXPECT_EQ(out_id, user_id);

    RedisDao::InvalidateEmailMapping(ctx, email);
}

TEST_F(RedisDaoTest, InvalidateEmailMapping) {
    const std::string email = "invalidate_test@example.com";
    RedisDao::CacheEmailMapping(ctx, email, 100);

    EXPECT_TRUE(RedisDao::InvalidateEmailMapping(ctx, email));

    uint32_t out_id = 0;
    EXPECT_FALSE(RedisDao::GetUserIdByEmail(ctx, email, out_id));
}

TEST_F(RedisDaoTest, GetEmailMappingNotFound) {
    uint32_t out_id = 0;
    EXPECT_FALSE(RedisDao::GetUserIdByEmail(ctx, "no_such_email@example.com", out_id));
}

// ============================================================
// 验证码
// ============================================================

TEST_F(RedisDaoTest, SetAndConsumeVerificationCode) {
    const std::string email = "verify_test@example.com";
    const std::string code = "123456";

    // 设置验证码
    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 1, code, 300));

    // 获取并消耗
    std::string retrieved;
    EXPECT_TRUE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, retrieved));
    EXPECT_EQ(retrieved, code);

    // 第二次获取应失败（已被消耗）
    std::string again;
    EXPECT_FALSE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, again));
}

TEST_F(RedisDaoTest, VerificationCodeExpiration) {
    const std::string email = "expire_test@example.com";
    const std::string code = "654321";

    // 设置 1 秒过期的验证码
    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 1, code, 1));

    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 获取应失败
    std::string retrieved;
    EXPECT_FALSE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, retrieved));
}

TEST_F(RedisDaoTest, VerificationCodeOverwrite) {
    const std::string email = "overwrite_code@example.com";

    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 1, "111111", 300));
    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 1, "222222", 300));

    // 应获取最新码
    std::string code;
    EXPECT_TRUE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, code));
    EXPECT_EQ(code, "222222");
}

TEST_F(RedisDaoTest, DifferentTypeCodes) {
    const std::string email = "multitype@example.com";

    // 注册验证码 (type=1)
    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 1, "reg_code", 300));
    // 重置密码验证码 (type=2)
    EXPECT_TRUE(RedisDao::SetVerificationCode(ctx, email, 2, "reset_code", 300));

    // 分别验证，互不干扰
    std::string code1, code2;
    EXPECT_TRUE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, code1));
    EXPECT_EQ(code1, "reg_code");

    EXPECT_TRUE(RedisDao::GetAndConsumeVerificationCode(ctx, email, 2, code2));
    EXPECT_EQ(code2, "reset_code");
}

// ============================================================
// 在线状态
// ============================================================

TEST_F(RedisDaoTest, SetAndCheckOnlineStatus) {
    uint32_t user_id = 77777;

    // 初始不在线
    EXPECT_FALSE(RedisDao::IsUserOnline(ctx, user_id));

    // 设为在线
    EXPECT_TRUE(RedisDao::SetUserOnline(ctx, user_id, 60));
    EXPECT_TRUE(RedisDao::IsUserOnline(ctx, user_id));

    // 设为离线
    EXPECT_TRUE(RedisDao::SetUserOffline(ctx, user_id));
    EXPECT_FALSE(RedisDao::IsUserOnline(ctx, user_id));
}

TEST_F(RedisDaoTest, OnlineStatusTTLExpiration) {
    uint32_t user_id = 88888;

    // 设置 1 秒过期
    EXPECT_TRUE(RedisDao::SetUserOnline(ctx, user_id, 1));
    EXPECT_TRUE(RedisDao::IsUserOnline(ctx, user_id));

    // 等待过期
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_FALSE(RedisDao::IsUserOnline(ctx, user_id));
}

TEST_F(RedisDaoTest, UpdateOnlineStatusTTL) {
    uint32_t user_id = 99999;

    // 先设置短 TTL
    EXPECT_TRUE(RedisDao::SetUserOnline(ctx, user_id, 2));

    // 用更长 TTL 刷新
    EXPECT_TRUE(RedisDao::SetUserOnline(ctx, user_id, 300));
    EXPECT_TRUE(RedisDao::IsUserOnline(ctx, user_id));

    // 清理
    RedisDao::SetUserOffline(ctx, user_id);
}

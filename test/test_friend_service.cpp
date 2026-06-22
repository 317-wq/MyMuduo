/*
    FriendService 业务层集成测试

    测试流程：
    1. 搜索用户
    2. 发送好友请求 → 同意 → 双向好友
    3. 拒绝好友请求
    4. 删除好友
    5. 好友列表（含 Redis 缓存）

    依赖：MySQL + Redis
*/

#include "service/FriendService.h"
#include "db/Database.h"
#include "cache/RedisCache.h"
#include "net/EventLoop.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

static const std::string DB_HOST = "127.0.0.1";
static const int DB_PORT = 3306;
static const std::string DB_USER = "root";
static const std::string DB_PASS = "lijiatong344A@";
static const std::string DB_NAME = "mymuduo";

static void WaitForLoop(EventLoop& loop, int max_retries = 100) {
    for (int i = 0; i < max_retries; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        loop.DoPendingFunctors();
    }
}

static std::string MakeTestEmail(const std::string& prefix) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return prefix + "_" + std::to_string(now) + "@test.com";
}

class FriendServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 检查 Redis
        {
            redisContext* ctx = redisConnect("127.0.0.1", 6379);
            if (ctx == nullptr || ctx->err) {
                if (ctx) redisFree(ctx);
                GTEST_SKIP() << "Redis not available";
            }
            redisFree(ctx);
        }

        _db = std::make_unique<Database>(DB_HOST, DB_PORT,
            DB_USER, DB_PASS, DB_NAME, 2);
        _redis = std::make_unique<RedisCache>("127.0.0.1", 6379, 2);
        _service = std::make_unique<FriendService>(_db.get(), &_loop);
        _service->SetRedisCache(_redis.get());

        // 创建测试用户
        _email_a = MakeTestEmail("fs_a");
        _email_b = MakeTestEmail("fs_b");

        bool done = false;
        _db->Execute(&_loop,
            [this](sql::Connection* conn) {
                std::string salt = "testsalt12345678";
                std::string hash = "testhash";
                UserDao::InsertUser(conn, _email_a, hash, salt, "FSA_User", _id_a);
                UserDao::InsertUser(conn, _email_b, hash, salt, "FSB_User", _id_b);
            },
            [&]() { done = true; });
        WaitForLoop(_loop);
        ASSERT_GT(_id_a, 0u);
        ASSERT_GT(_id_b, 0u);
    }

    void TearDown() override {
        // 清理好友关系
        if (_id_a > 0 && _id_b > 0) {
            bool done = false;
            _db->Execute(&_loop,
                [this](sql::Connection* conn) {
                    FriendDao::DeleteFriend(conn, _id_a, _id_b);
                },
                [&]() { done = true; });
            WaitForLoop(_loop);
        }
        _service.reset();
        _redis.reset();
        _db.reset();
    }

    EventLoop _loop;
    std::unique_ptr<Database> _db;
    std::unique_ptr<RedisCache> _redis;
    std::unique_ptr<FriendService> _service;

    std::string _email_a, _email_b;
    uint32_t _id_a = 0, _id_b = 0;
};

TEST_F(FriendServiceTest, SearchUser) {
    bool done = false;
    int err = -1;
    std::vector<UserInfo> results;

    _service->SearchUser(_email_b, _id_a,
        [&](int e, const std::string&, const std::vector<UserInfo>& users) {
            err = e;
            results = users;
            done = true;
        });
    WaitForLoop(_loop);

    EXPECT_TRUE(done);
    EXPECT_EQ(err, 0);
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].email, _email_b);
}

TEST_F(FriendServiceTest, SendAndAcceptFriendRequest) {
    // Step 1: 发送请求
    bool done = false;
    int err = -1;
    _service->SendFriendRequest(_id_a, _id_b,
        [&](int e, const std::string&) { err = e; done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(err, 0);

    // Step 2: 查待处理请求
    done = false;
    std::vector<FriendRequest> pending;
    _service->GetPendingRequests(_id_b,
        [&](int e, const std::string&, const std::vector<FriendRequest>& reqs) {
            err = e;
            pending = reqs;
            done = true;
        });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].from_user_id, _id_a);

    // Step 3: 同意
    done = false;
    uint32_t accepted_friend_id = 0;
    _service->AcceptFriendRequest(_id_b, pending[0].id,
        [&](int e, const std::string&, uint32_t fid,
            const std::string&, const std::string&, const std::string&) {
            err = e;
            accepted_friend_id = fid;
            done = true;
        });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(accepted_friend_id, _id_a);

    // Step 4: 好友列表
    done = false;
    std::vector<FriendInfo> friends;
    _service->GetFriendList(_id_a,
        [&](int e, const std::string&, const std::vector<FriendInfo>& list) {
            err = e;
            friends = list;
            done = true;
        });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);
    ASSERT_EQ(friends.size(), 1u);
    EXPECT_EQ(friends[0].friend_id, _id_b);
    EXPECT_EQ(friends[0].username, "FSB_User");
}

TEST_F(FriendServiceTest, RejectFriendRequest) {
    // 发送请求
    bool done = false;
    _service->SendFriendRequest(_id_a, _id_b,
        [&](int, const std::string&) { done = true; });
    WaitForLoop(_loop);

    // 查请求
    done = false;
    std::vector<FriendRequest> pending;
    _service->GetPendingRequests(_id_b,
        [&](int, const std::string&, const std::vector<FriendRequest>& reqs) {
            pending = reqs;
            done = true;
        });
    WaitForLoop(_loop);
    ASSERT_EQ(pending.size(), 1u);

    // 拒绝
    done = false;
    int err = -1;
    _service->RejectFriendRequest(_id_b, pending[0].id,
        [&](int e, const std::string&) { err = e; done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);

    // 待处理列表为空
    done = false;
    _service->GetPendingRequests(_id_b,
        [&](int, const std::string&, const std::vector<FriendRequest>& reqs) {
            pending = reqs;
            done = true;
        });
    WaitForLoop(_loop);
    EXPECT_EQ(pending.size(), 0u);
}

TEST_F(FriendServiceTest, DeleteFriend) {
    // 先建立好友关系
    bool done = false;
    _service->SendFriendRequest(_id_a, _id_b,
        [&](int, const std::string&) { done = true; });
    WaitForLoop(_loop);

    done = false;
    std::vector<FriendRequest> pending;
    _service->GetPendingRequests(_id_b,
        [&](int, const std::string&, const std::vector<FriendRequest>& reqs) {
            pending = reqs; done = true; });
    WaitForLoop(_loop);
    ASSERT_EQ(pending.size(), 1u);

    done = false;
    _service->AcceptFriendRequest(_id_b, pending[0].id,
        [&](int, const std::string&, uint32_t, const std::string&,
            const std::string&, const std::string&) { done = true; });
    WaitForLoop(_loop);

    // 删除好友
    done = false;
    int err = -1;
    _service->DeleteFriend(_id_a, _id_b,
        [&](int e, const std::string&) { err = e; done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);

    // 好友列表为空
    done = false;
    std::vector<FriendInfo> friends;
    _service->GetFriendList(_id_a,
        [&](int e, const std::string&, const std::vector<FriendInfo>& list) {
            err = e; friends = list; done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(friends.size(), 0u);
}

TEST_F(FriendServiceTest, EmptyFriendList) {
    bool done = false;
    int err = -1;
    std::vector<FriendInfo> friends;
    _service->GetFriendList(_id_a,
        [&](int e, const std::string&, const std::vector<FriendInfo>& list) {
            err = e; friends = list; done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(friends.size(), 0u);
}

/*
    FriendDao 数据访问层单元测试

    覆盖：
    - 用户搜索（精确/模糊/排除自己）
    - 好友请求（发送/同意/拒绝）
    - 好友管理（列表/删除/检查）
    - 双向好友关系验证

    依赖：MySQL 必须运行，且 users 表中有测试用户
*/

#include "db/Database.h"
#include "db/FriendDao.h"
#include "db/UserDao.h"
#include "net/EventLoop.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

static const std::string DB_HOST = "127.0.0.1";
static const int DB_PORT = 3306;
static const std::string DB_USER = "root";
static const std::string DB_PASS = "lijiatong344A@";
static const std::string DB_NAME = "mymuduo";

// 生成唯一测试邮箱
static std::string MakeTestEmail(const std::string& prefix) {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return prefix + "_" + std::to_string(now) + "@test.com";
}

static void WaitForLoop(EventLoop& loop, int max_retries = 50) {
    for (int i = 0; i < max_retries; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        loop.DoPendingFunctors();
    }
}

class FriendDaoTest : public ::testing::Test {
protected:
    void SetUp() override {
        _db = std::make_unique<Database>(DB_HOST, DB_PORT,
            DB_USER, DB_PASS, DB_NAME, 2);

        // 创建两个测试用户
        _email_a = MakeTestEmail("friend_test_a");
        _email_b = MakeTestEmail("friend_test_b");

        bool done_a = false, done_b = false;
        _db->Execute(&_loop,
            [this](sql::Connection* conn) {
                std::string salt = "aaaabbbbccccdddd";
                std::string hash = "deadbeef";  // fake hash
                UserDao::InsertUser(conn, _email_a, hash, salt, "FriendUserA", _user_id_a);
                UserDao::InsertUser(conn, _email_b, hash, salt, "FriendUserB", _user_id_b);
            },
            [&]() { done_a = true; done_b = true; });
        WaitForLoop(_loop);

        ASSERT_GT(_user_id_a, 0u);
        ASSERT_GT(_user_id_b, 0u);
    }

    void TearDown() override {
        // 清理好友关系
        if (_user_id_a > 0 && _user_id_b > 0) {
            bool cleaned = false;
            _db->Execute(&_loop,
                [this](sql::Connection* conn) {
                    FriendDao::DeleteFriend(conn, _user_id_a, _user_id_b);
                },
                [&]() { cleaned = true; });
            WaitForLoop(_loop);
        }
        _db.reset();
    }

    std::unique_ptr<Database> _db;
    EventLoop _loop;

    std::string _email_a, _email_b;
    uint32_t _user_id_a = 0, _user_id_b = 0;
};

// ============================================================
// 用户搜索
// ============================================================

TEST_F(FriendDaoTest, SearchUserByExactEmail) {
    std::vector<UserInfo> results;
    bool done = false;

    _db->Execute(&_loop,
        [this, &results](sql::Connection* conn) {
            FriendDao::SearchUserByEmail(conn, _email_b, _user_id_a, results);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    EXPECT_TRUE(done);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, _user_id_b);
    EXPECT_EQ(results[0].email, _email_b);
    EXPECT_EQ(results[0].username, "FriendUserB");
}

TEST_F(FriendDaoTest, SearchUserByFuzzyEmail) {
    std::vector<UserInfo> results;
    bool done = false;

    _db->Execute(&_loop,
        [this, &results](sql::Connection* conn) {
            // 用 email 的一部分搜索
            FriendDao::SearchUserByEmail(conn, "friend_test_b", _user_id_a, results);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    EXPECT_TRUE(done);
    EXPECT_GE(results.size(), 1u);
    // 应包含 B 但不应包含 A
    bool found_b = false, found_a = false;
    for (auto& u : results) {
        if (u.id == _user_id_b) found_b = true;
        if (u.id == _user_id_a) found_a = true;
    }
    EXPECT_TRUE(found_b);
    EXPECT_FALSE(found_a);
}

TEST_F(FriendDaoTest, SearchUserExcludesSelf) {
    std::vector<UserInfo> results;
    bool done = false;

    _db->Execute(&_loop,
        [this, &results](sql::Connection* conn) {
            FriendDao::SearchUserByEmail(conn, _email_a, _user_id_a, results);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    EXPECT_TRUE(done);
    // 不能搜到自己
    for (auto& u : results) {
        EXPECT_NE(u.id, _user_id_a);
    }
}

// ============================================================
// 好友请求
// ============================================================

TEST_F(FriendDaoTest, SendAndAcceptFriendRequest) {
    uint32_t request_id = 0;
    bool auto_accepted = false;
    bool done = false;

    // Step 1: A 向 B 发请求
    _db->Execute(&_loop,
        [this, &request_id, &auto_accepted](sql::Connection* conn) {
            FriendDao::SendFriendRequest(conn, _user_id_a, _user_id_b,
                                         request_id, auto_accepted);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    EXPECT_TRUE(done);
    EXPECT_GT(request_id, 0u);
    EXPECT_FALSE(auto_accepted);

    // Step 2: B 还没有好友
    std::vector<FriendInfo> friends;
    done = false;
    _db->Execute(&_loop,
        [this, &friends](sql::Connection* conn) {
            FriendDao::GetFriendList(conn, _user_id_b, friends);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(friends.size(), 0u);

    // Step 3: B 查看待处理请求
    std::vector<FriendRequest> pending;
    done = false;
    _db->Execute(&_loop,
        [this, &pending](sql::Connection* conn) {
            FriendDao::GetPendingRequests(conn, _user_id_b, pending);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].from_user_id, _user_id_a);
    EXPECT_EQ(pending[0].from_username, "FriendUserA");

    // Step 4: B 同意请求
    uint32_t accepted_friend_id = 0;
    done = false;
    _db->Execute(&_loop,
        [this, request_id, &accepted_friend_id](sql::Connection* conn) {
            FriendDao::AcceptFriendRequest(conn, _user_id_b, request_id,
                                           accepted_friend_id);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_EQ(accepted_friend_id, _user_id_a);

    // Step 5: 双向好友关系验证
    bool a_friend_of_b = false, b_friend_of_a = false;
    done = false;
    _db->Execute(&_loop,
        [this, &a_friend_of_b, &b_friend_of_a](sql::Connection* conn) {
            a_friend_of_b = FriendDao::IsFriend(conn, _user_id_b, _user_id_a);
            b_friend_of_a = FriendDao::IsFriend(conn, _user_id_a, _user_id_b);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_TRUE(a_friend_of_b);
    EXPECT_TRUE(b_friend_of_a);

    // Step 6: 双方好友列表各有一人
    friends.clear();
    done = false;
    _db->Execute(&_loop,
        [this, &friends](sql::Connection* conn) {
            FriendDao::GetFriendList(conn, _user_id_a, friends);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(friends.size(), 1u);
    EXPECT_EQ(friends[0].friend_id, _user_id_b);

    friends.clear();
    done = false;
    _db->Execute(&_loop,
        [this, &friends](sql::Connection* conn) {
            FriendDao::GetFriendList(conn, _user_id_b, friends);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(friends.size(), 1u);
    EXPECT_EQ(friends[0].friend_id, _user_id_a);
}

TEST_F(FriendDaoTest, RejectFriendRequest) {
    // A 发请求
    uint32_t request_id = 0;
    bool auto_accepted = false;
    bool done = false;
    _db->Execute(&_loop,
        [this, &request_id, &auto_accepted](sql::Connection* conn) {
            FriendDao::SendFriendRequest(conn, _user_id_a, _user_id_b,
                                         request_id, auto_accepted);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_GT(request_id, 0u);

    // B 拒绝
    done = false;
    bool rejected = false;
    _db->Execute(&_loop,
        [this, request_id, &rejected](sql::Connection* conn) {
            rejected = FriendDao::RejectFriendRequest(conn, _user_id_b, request_id);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_TRUE(rejected);

    // 待处理列表应为空
    std::vector<FriendRequest> pending;
    done = false;
    _db->Execute(&_loop,
        [this, &pending](sql::Connection* conn) {
            FriendDao::GetPendingRequests(conn, _user_id_b, pending);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(pending.size(), 0u);
}

TEST_F(FriendDaoTest, DeleteFriend) {
    // 先建立好友关系（A→B 请求 → B 同意）
    uint32_t request_id = 0;
    bool auto_accepted = false;
    bool done = false;
    _db->Execute(&_loop,
        [this, &request_id, &auto_accepted](sql::Connection* conn) {
            FriendDao::SendFriendRequest(conn, _user_id_a, _user_id_b,
                                         request_id, auto_accepted);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    uint32_t accepted_id = 0;
    done = false;
    _db->Execute(&_loop,
        [this, request_id, &accepted_id](sql::Connection* conn) {
            FriendDao::AcceptFriendRequest(conn, _user_id_b, request_id, accepted_id);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);

    // 删除好友
    done = false;
    bool deleted = false;
    _db->Execute(&_loop,
        [this, &deleted](sql::Connection* conn) {
            deleted = FriendDao::DeleteFriend(conn, _user_id_a, _user_id_b);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_TRUE(done);
    EXPECT_TRUE(deleted);

    // 双向都不再是好友
    done = false;
    bool a_is_friend = false, b_is_friend = false;
    _db->Execute(&_loop,
        [this, &a_is_friend, &b_is_friend](sql::Connection* conn) {
            a_is_friend = FriendDao::IsFriend(conn, _user_id_a, _user_id_b);
            b_is_friend = FriendDao::IsFriend(conn, _user_id_b, _user_id_a);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_FALSE(a_is_friend);
    EXPECT_FALSE(b_is_friend);
}

TEST_F(FriendDaoTest, NotFriendByDefault) {
    bool done = false;
    bool is_friend = true;
    _db->Execute(&_loop,
        [this, &is_friend](sql::Connection* conn) {
            is_friend = FriendDao::IsFriend(conn, _user_id_a, _user_id_b);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_FALSE(is_friend);
}

TEST_F(FriendDaoTest, EmptyFriendList) {
    std::vector<FriendInfo> friends;
    bool done = false;
    _db->Execute(&_loop,
        [this, &friends](sql::Connection* conn) {
            FriendDao::GetFriendList(conn, _user_id_a, friends);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(friends.size(), 0u);
}

TEST_F(FriendDaoTest, EmptyPendingRequests) {
    std::vector<FriendRequest> pending;
    bool done = false;
    _db->Execute(&_loop,
        [this, &pending](sql::Connection* conn) {
            FriendDao::GetPendingRequests(conn, _user_id_a, pending);
        },
        [&]() { done = true; });
    WaitForLoop(_loop);
    EXPECT_EQ(pending.size(), 0u);
}

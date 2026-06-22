#pragma once

/*
    好友业务逻辑层

    职责：
    - 协调 FriendDao (MySQL) + RedisDao (缓存) 完成好友相关操作
    - 好友列表采用 Cache-Aside：先 Redis Set → miss 查 MySQL → 回填
    - 好友增删时主动更新 Redis 缓存

    callback 约定：
    - error_code: 0 = 成功，非 0 = 失败
    - msg: 错误描述或成功提示
*/

#include "db/Database.h"
#include "db/FriendDao.h"
#include "cache/RedisCache.h"
#include "cache/RedisDao.h"
#include "net/EventLoop.h"

#include <functional>
#include <string>
#include <vector>
#include <cstdint>

class FriendService : NoCopy {
public:
    using SimpleCallback = std::function<void(int error_code, const std::string& msg)>;

    using SearchCallback = std::function<void(int error_code, const std::string& msg,
                                              const std::vector<UserInfo>& users)>;

    using AcceptCallback = std::function<void(int error_code, const std::string& msg,
                                              uint32_t friend_id,
                                              const std::string& friend_email,
                                              const std::string& friend_username,
                                              const std::string& friend_avatar)>;

    using FriendListCallback = std::function<void(int error_code, const std::string& msg,
                                                  const std::vector<FriendInfo>& friends)>;

    using PendingCallback = std::function<void(int error_code, const std::string& msg,
                                               const std::vector<FriendRequest>& requests)>;

    FriendService(Database* db, EventLoop* loop)
        : _db(db), _loop(loop) {}

    void SetRedisCache(RedisCache* redis) { _redis = redis; }

    // ---------- 用户搜索 ----------
    void SearchUser(const std::string& keyword, uint32_t exclude_user_id,
                    SearchCallback callback);

    // ---------- 好友请求 ----------
    void SendFriendRequest(uint32_t from_id, uint32_t to_id,
                           SimpleCallback callback);
    void AcceptFriendRequest(uint32_t user_id, uint32_t request_id,
                             AcceptCallback callback);
    void RejectFriendRequest(uint32_t user_id, uint32_t request_id,
                             SimpleCallback callback);

    // ---------- 好友管理 ----------
    void DeleteFriend(uint32_t user_id, uint32_t friend_id,
                      SimpleCallback callback);
    void GetFriendList(uint32_t user_id, FriendListCallback callback);
    void GetPendingRequests(uint32_t user_id, PendingCallback callback);

private:
    // Cache-Aside 的 MySQL 回退 + 回填
    void GetFriendListFromMySQL(uint32_t user_id, FriendListCallback callback);

    // 更新双方好友列表缓存
    void UpdateFriendCacheBoth(uint32_t user_a, uint32_t user_b, bool add);

    Database* _db;
    EventLoop* _loop;
    RedisCache* _redis = nullptr;
};

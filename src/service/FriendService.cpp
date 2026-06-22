#include "service/FriendService.h"
#include "net/Log.h"

#include <memory>
#include <algorithm>

// ============================================================
// 搜索用户
// ============================================================

void FriendService::SearchUser(const std::string& keyword, uint32_t exclude_user_id,
                                SearchCallback callback)
{
    auto users = std::make_shared<std::vector<UserInfo>>();
    struct Result {
        int error_code = 0;
        std::string msg;
    };
    auto result = std::make_shared<Result>();

    _db->Execute(_loop,
        [keyword, exclude_user_id, users, result](sql::Connection* conn) {
            FriendDao::SearchUserByEmail(conn, keyword, exclude_user_id, *users);
            result->error_code = 0;
            result->msg = users->empty() ? "未找到匹配用户" : "搜索成功";
        },
        [users, result, callback = std::move(callback)]() {
            callback(result->error_code, result->msg, *users);
        });
}

// ============================================================
// 发送好友请求
// ============================================================

void FriendService::SendFriendRequest(uint32_t from_id, uint32_t to_id,
                                       SimpleCallback callback)
{
    auto result = std::make_shared<std::pair<int, std::string>>();
    auto request_id = std::make_shared<uint32_t>(0);
    auto auto_accepted = std::make_shared<bool>(false);

    _db->Execute(_loop,
        [from_id, to_id, request_id, auto_accepted, result](sql::Connection* conn) {
            if (FriendDao::SendFriendRequest(conn, from_id, to_id,
                                             *request_id, *auto_accepted)) {
                if (*auto_accepted) {
                    result->first = 0;
                    result->second = "你们已成为好友（对方已发来请求）";
                } else {
                    result->first = 0;
                    result->second = "好友请求已发送";
                }
            } else {
                result->first = 1;
                result->second = "发送失败（可能是重复请求或已是好友）";
            }
        },
        [this, from_id, to_id, auto_accepted, result,
         callback = std::move(callback)]()
        {
            if (result->first == 0 && _redis) {
                if (*auto_accepted) {
                    // 自动成为好友 → 更新双方缓存
                    UpdateFriendCacheBoth(from_id, to_id, /*add=*/true);
                }
            }
            callback(result->first, result->second);
        });
}

// ============================================================
// 同意好友请求
// ============================================================

void FriendService::AcceptFriendRequest(uint32_t user_id, uint32_t request_id,
                                         AcceptCallback callback)
{
    auto result = std::make_shared<std::pair<int, std::string>>();
    auto friend_id = std::make_shared<uint32_t>(0);
    auto friend_info = std::make_shared<UserInfo>();

    _db->Execute(_loop,
        [user_id, request_id, friend_id, result](sql::Connection* conn) {
            if (!FriendDao::AcceptFriendRequest(conn, user_id, request_id, *friend_id)) {
                result->first = 1;
                result->second = "同意失败：请求不存在或已处理";
                return;
            }
            result->first = 0;
            result->second = "已添加为好友";
        },
        [this, user_id, friend_id, friend_info, result,
         callback = std::move(callback)]()
        {
            if (result->first == 0 && *friend_id > 0) {
                // 更新 Redis 缓存
                if (_redis) {
                    UpdateFriendCacheBoth(user_id, *friend_id, /*add=*/true);
                }

                // 异步查好友信息
                _db->Execute(_loop,
                    [friend_id, friend_info](sql::Connection* conn) {
                        UserDao::GetUserById(conn, *friend_id, *friend_info);
                    },
                    [result, friend_info, callback = std::move(callback)]() {
                        callback(result->first, result->second,
                                 friend_info->id, friend_info->email,
                                 friend_info->username, friend_info->avatar);
                    });
            } else {
                callback(result->first, result->second, 0, "", "", "");
            }
        });
}

// ============================================================
// 拒绝好友请求
// ============================================================

void FriendService::RejectFriendRequest(uint32_t user_id, uint32_t request_id,
                                         SimpleCallback callback)
{
    auto result = std::make_shared<std::pair<int, std::string>>();

    _db->Execute(_loop,
        [user_id, request_id, result](sql::Connection* conn) {
            if (FriendDao::RejectFriendRequest(conn, user_id, request_id)) {
                result->first = 0;
                result->second = "已拒绝";
            } else {
                result->first = 1;
                result->second = "拒绝失败：请求不存在";
            }
        },
        [result, callback = std::move(callback)]() {
            callback(result->first, result->second);
        });
}

// ============================================================
// 删除好友
// ============================================================

void FriendService::DeleteFriend(uint32_t user_id, uint32_t friend_id,
                                  SimpleCallback callback)
{
    auto result = std::make_shared<std::pair<int, std::string>>();

    _db->Execute(_loop,
        [user_id, friend_id, result](sql::Connection* conn) {
            if (FriendDao::DeleteFriend(conn, user_id, friend_id)) {
                result->first = 0;
                result->second = "已删除好友";
            } else {
                result->first = 1;
                result->second = "删除失败";
            }
        },
        [this, user_id, friend_id, result, callback = std::move(callback)]() {
            if (result->first == 0 && _redis) {
                UpdateFriendCacheBoth(user_id, friend_id, /*add=*/false);
            }
            callback(result->first, result->second);
        });
}

// ============================================================
// 好友列表（Cache-Aside）
// ============================================================

void FriendService::GetFriendList(uint32_t user_id, FriendListCallback callback)
{
    if (!_redis) {
        GetFriendListFromMySQL(user_id, std::move(callback));
        return;
    }

    auto friend_infos = std::make_shared<std::vector<FriendInfo>>();
    auto cache_hit = std::make_shared<bool>(false);

    // Step 1: 查 Redis 缓存
    _redis->Execute(_loop,
        [user_id, friend_infos, cache_hit](redisContext* ctx) {
            std::vector<uint32_t> friend_ids;
            if (RedisDao::GetFriendIdSet(ctx, user_id, friend_ids) && !friend_ids.empty()) {
                // 逐个获取好友信息 + 在线状态
                for (auto fid : friend_ids) {
                    UserInfo u;
                    if (RedisDao::GetCachedUserInfo(ctx, fid, u)) {
                        FriendInfo fi;
                        fi.friend_id  = u.id;
                        fi.email      = u.email;
                        fi.username   = u.username;
                        fi.avatar     = u.avatar;
                        fi.online     = RedisDao::IsUserOnline(ctx, fid);
                        fi.created_at = "";  // 缓存不存 created_at
                        friend_infos->push_back(fi);
                    }
                }
                // 只有在至少找到一个好友完整信息时才标记缓存命中
                *cache_hit = !friend_infos->empty();
            }
        },
        [this, user_id, friend_infos, cache_hit, callback = std::move(callback)]() {
            if (*cache_hit) {
                callback(0, "ok", *friend_infos);
                return;
            }
            // Cache miss → MySQL
            GetFriendListFromMySQL(user_id, std::move(callback));
        });
}

void FriendService::GetFriendListFromMySQL(uint32_t user_id, FriendListCallback callback)
{
    auto friends = std::make_shared<std::vector<FriendInfo>>();
    auto result = std::make_shared<std::pair<int, std::string>>();

    _db->Execute(_loop,
        [user_id, friends, result](sql::Connection* conn) {
            if (!FriendDao::GetFriendList(conn, user_id, *friends)) {
                result->first = 1;
                result->second = "查询失败";
                return;
            }
            result->first = 0;
            result->second = "ok";
        },
        [this, user_id, friends, result, callback = std::move(callback)]() {
            if (result->first == 0 && _redis && !friends->empty()) {
                // 回填 Redis 缓存
                std::vector<uint32_t> friend_ids;
                for (auto& f : *friends) {
                    friend_ids.push_back(f.friend_id);
                }
                _redis->Execute(nullptr,
                    [user_id, friend_ids](redisContext* ctx) {
                        RedisDao::CacheFriendIdSet(ctx, user_id, friend_ids, 600);
                    },
                    nullptr);
            }
            callback(result->first, result->second, *friends);
        });
}

// ============================================================
// 待处理请求
// ============================================================

void FriendService::GetPendingRequests(uint32_t user_id, PendingCallback callback)
{
    auto requests = std::make_shared<std::vector<FriendRequest>>();
    auto result = std::make_shared<std::pair<int, std::string>>();

    _db->Execute(_loop,
        [user_id, requests, result](sql::Connection* conn) {
            FriendDao::GetPendingRequests(conn, user_id, *requests);
            result->first = 0;
            result->second = "ok";
        },
        [requests, result, callback = std::move(callback)]() {
            callback(result->first, result->second, *requests);
        });
}

// ============================================================
// 缓存工具
// ============================================================

void FriendService::UpdateFriendCacheBoth(uint32_t user_a, uint32_t user_b, bool add)
{
    if (!_redis) return;

    _redis->Execute(nullptr,
        [user_a, user_b, add](redisContext* ctx) {
            if (add) {
                RedisDao::AddFriendToSet(ctx, user_a, user_b);
                RedisDao::AddFriendToSet(ctx, user_b, user_a);
            } else {
                RedisDao::RemoveFriendFromSet(ctx, user_a, user_b);
                RedisDao::RemoveFriendFromSet(ctx, user_b, user_a);
            }
        },
        nullptr);
}

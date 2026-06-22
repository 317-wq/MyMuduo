#pragma once

/*
    Redis 数据访问对象 (Data Access Object)

    所有方法都是同步的，接收 redisContext*，
    由调用者通过 RedisCache::Execute 在 Redis worker 线程执行。

    Key 命名规范：
    - 用户信息 Hash:   user:info:<id>
    - 邮箱映射 String:  user:email:<email>
    - 验证码 String:    code:<type>:<email>
    - 在线状态 String:  online:<user_id>
*/

#include "db/UserDao.h"

extern "C" {
#include <hiredis/hiredis.h>
}

#include <string>
#include <vector>
#include <cstdint>

class RedisDao {
public:
    // ============================================================
    // 用户信息缓存（Hash）
    // ============================================================

    // 将 UserInfo 写入 Redis Hash
    // Key: user:info:<id>
    // Fields: email, username, password, salt, avatar, created_at
    static bool CacheUserInfo(redisContext* ctx, const UserInfo& user);

    // 从 Redis Hash 读取 UserInfo
    // 返回 true 表示命中，false 表示未命中或出错
    static bool GetCachedUserInfo(redisContext* ctx, uint32_t user_id, UserInfo& out);

    // 失效用户缓存（DELETE key）
    static bool InvalidateUserCache(redisContext* ctx, uint32_t user_id);

    // ============================================================
    // 邮箱 → 用户 ID 映射（String）
    // ============================================================

    // 设置邮箱到 ID 的映射
    // Key: user:email:<email>  Value: user_id (string)
    static bool CacheEmailMapping(redisContext* ctx, const std::string& email, uint32_t user_id);

    // 根据邮箱获取用户 ID
    // 返回 true 表示命中，false 表示未命中
    static bool GetUserIdByEmail(redisContext* ctx, const std::string& email, uint32_t& out_id);

    // 失效邮箱映射
    static bool InvalidateEmailMapping(redisContext* ctx, const std::string& email);

    // ============================================================
    // 验证码（String + TTL）
    // ============================================================

    // 设置验证码（自动过期）
    // Key: code:<type>:<email>  Value: code, TTL: expire_seconds
    // type: 1=注册, 2=重置密码
    static bool SetVerificationCode(redisContext* ctx, const std::string& email,
                                    int type, const std::string& code, int expire_seconds);

    // 获取并消耗验证码（验证成功后立即删除）
    // 返回 true 表示验证码匹配且成功删除
    static bool GetAndConsumeVerificationCode(redisContext* ctx, const std::string& email,
                                              int type, std::string& out_code);

    // ============================================================
    // 在线状态（String + TTL）
    // ============================================================

    // 设置用户在线
    // Key: online:<user_id>  Value: "1", TTL: ttl_seconds
    static bool SetUserOnline(redisContext* ctx, uint32_t user_id, int ttl_seconds);

    // 检查用户是否在线
    static bool IsUserOnline(redisContext* ctx, uint32_t user_id);

    // 设置用户离线
    static bool SetUserOffline(redisContext* ctx, uint32_t user_id);

    // ============================================================
    // 好友列表缓存（Set）
    // ============================================================

    // Set: friend:list:<user_id> → {friend_id, friend_id, ...}
    // 获取好友 ID 集合
    static bool GetFriendIdSet(redisContext* ctx, uint32_t user_id,
                               std::vector<uint32_t>& out_friend_ids);

    // 缓存好友 ID 集合（批量 SADD + EXPIRE）
    static bool CacheFriendIdSet(redisContext* ctx, uint32_t user_id,
                                 const std::vector<uint32_t>& friend_ids, int ttl_seconds);

    // 添加一个好友到集合
    static bool AddFriendToSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id);

    // 从集合移除一个好友
    static bool RemoveFriendFromSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id);

    // 判断是否为好友（集合中存在）
    static bool IsInFriendSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id);

    // ============================================================
    // 搜索历史（List，最近 6 条）
    // ============================================================

    // List: search:history:<user_id>
    // 添加一条搜索记录（LPUSH + LTRIM 保留最近 6 条）
    static bool AddSearchHistory(redisContext* ctx, uint32_t user_id,
                                 const std::string& keyword);

    // 获取搜索历史（LRANGE 0 5）
    static bool GetSearchHistory(redisContext* ctx, uint32_t user_id,
                                 std::vector<std::string>& out);

private:
    // 工具：构造带前缀的 key
    static std::string MakeUserInfoKey(uint32_t user_id);
    static std::string MakeEmailKey(const std::string& email);
    static std::string MakeCodeKey(int type, const std::string& email);
    static std::string MakeOnlineKey(uint32_t user_id);

    // 工具：检查 reply 状态
    static bool IsReplyOK(redisReply* reply);
};

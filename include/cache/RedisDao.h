#pragma once

#include "db/UserDao.h"

#include <sw/redis++/redis++.h>

#include <cstdint>
#include <string>
#include <vector>

// RedisDao 封装用户、好友、验证码和在线状态相关的缓存操作。
class RedisDao {
public:
    // 将用户信息写入 Redis 哈希缓存。
    static bool CacheUserInfo(sw::redis::Redis* redis, const UserInfo& user);

    // 从 Redis 哈希缓存读取用户信息。
    static bool GetCachedUserInfo(sw::redis::Redis* redis,
                                  uint32_t user_id,
                                  UserInfo& user);

    // 删除指定用户的信息缓存。
    static bool InvalidateUserCache(sw::redis::Redis* redis, uint32_t user_id);

    // 写入邮箱到用户 ID 的缓存映射。
    static bool CacheEmailMapping(sw::redis::Redis* redis,
                                  const std::string& email,
                                  uint32_t user_id);

    // 根据邮箱读取用户 ID。
    static bool GetUserIdByEmail(sw::redis::Redis* redis,
                                 const std::string& email,
                                 uint32_t& user_id);

    // 删除邮箱到用户 ID 的缓存映射。
    static bool InvalidateEmailMapping(sw::redis::Redis* redis,
                                       const std::string& email);

    // 写入带过期时间的验证码。
    static bool SetVerificationCode(sw::redis::Redis* redis,
                                    const std::string& email,
                                    int type,
                                    const std::string& code,
                                    int ttl_seconds);

    // 读取并删除验证码，保证验证码只能消费一次。
    static bool GetAndConsumeVerificationCode(sw::redis::Redis* redis,
                                               const std::string& email,
                                               int type,
                                               std::string& code);

    // 设置用户在线状态，并设置状态过期时间。
    static bool SetUserOnline(sw::redis::Redis* redis,
                              uint32_t user_id,
                              int ttl_seconds);

    // 判断用户是否处于在线状态。
    static bool IsUserOnline(sw::redis::Redis* redis, uint32_t user_id);

    // 删除用户在线状态。
    static bool SetUserOffline(sw::redis::Redis* redis, uint32_t user_id);

    // 读取用户的好友 ID 集合。
    static bool GetFriendIdSet(sw::redis::Redis* redis,
                               uint32_t user_id,
                               std::vector<uint32_t>& friend_ids);

    // 重建用户好友 ID 集合，并设置缓存过期时间。
    static bool CacheFriendIdSet(sw::redis::Redis* redis,
                                 uint32_t user_id,
                                 const std::vector<uint32_t>& friend_ids,
                                 int ttl_seconds);

    // 向用户好友集合中添加好友。
    static bool AddFriendToSet(sw::redis::Redis* redis,
                               uint32_t user_id,
                               uint32_t friend_id);

    // 从用户好友集合中删除好友。
    static bool RemoveFriendFromSet(sw::redis::Redis* redis,
                                    uint32_t user_id,
                                    uint32_t friend_id);

    // 判断指定用户是否在好友集合中。
    static bool IsInFriendSet(sw::redis::Redis* redis,
                              uint32_t user_id,
                              uint32_t friend_id);

    // 将关键词加入用户搜索历史，并限制历史记录长度。
    static bool AddSearchHistory(sw::redis::Redis* redis,
                                 uint32_t user_id,
                                 const std::string& keyword);

    // 读取用户最近的搜索历史。
    static bool GetSearchHistory(sw::redis::Redis* redis,
                                 uint32_t user_id,
                                 std::vector<std::string>& history);
};

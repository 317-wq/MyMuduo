#include "cache/RedisDao.h"

#include <cstring>
#include <sstream>

// ============================================================
// Key 构造工具
// ============================================================

std::string RedisDao::MakeUserInfoKey(uint32_t user_id) {
    return "user:info:" + std::to_string(user_id);
}

std::string RedisDao::MakeEmailKey(const std::string& email) {
    return "user:email:" + email;
}

std::string RedisDao::MakeCodeKey(int type, const std::string& email) {
    return "code:" + std::to_string(type) + ":" + email;
}

std::string RedisDao::MakeOnlineKey(uint32_t user_id) {
    return "online:" + std::to_string(user_id);
}

static std::string MakeFriendListKey(uint32_t user_id) {
    return "friend:list:" + std::to_string(user_id);
}

bool RedisDao::IsReplyOK(redisReply* reply) {
    if (!reply) return false;
    if (reply->type == REDIS_REPLY_ERROR) return false;
    return true;
}

// ============================================================
// 用户信息缓存（Hash）
// ============================================================

bool RedisDao::CacheUserInfo(redisContext* ctx, const UserInfo& user) {
    if (!ctx) return false;

    std::string key = MakeUserInfoKey(user.id);

    // 使用 redisCommandArgv 避免 printf 格式化参数数量不匹配
    const char* argv[] = {
        "HMSET", key.c_str(),
        "email",       user.email.c_str(),
        "username",    user.username.c_str(),
        "password",    user.password.c_str(),
        "salt",        user.salt.c_str(),
        "avatar",      user.avatar.c_str(),
        "created_at",  user.created_at.c_str()
    };
    size_t argvlen[] = {
        5, key.size(),
        5, user.email.size(),
        8, user.username.size(),
        8, user.password.size(),
        4, user.salt.size(),
        6, user.avatar.size(),
        10, user.created_at.size()
    };

    redisReply* reply = (redisReply*)redisCommandArgv(
        ctx, sizeof(argv) / sizeof(argv[0]), argv, argvlen);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisDao::GetCachedUserInfo(redisContext* ctx, uint32_t user_id, UserInfo& out) {
    if (!ctx) return false;

    std::string key = MakeUserInfoKey(user_id);

    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    out.id = user_id;

    // HGETALL 返回 [field1, value1, field2, value2, ...]
    for (size_t i = 0; i < reply->elements; i += 2) {
        if (i + 1 >= reply->elements) break;

        std::string field(reply->element[i]->str, reply->element[i]->len);
        std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);

        if (field == "email")       out.email = value;
        else if (field == "username")   out.username = value;
        else if (field == "password")   out.password = value;
        else if (field == "salt")        out.salt = value;
        else if (field == "avatar")      out.avatar = value;
        else if (field == "created_at")  out.created_at = value;
    }

    freeReplyObject(reply);

    // 至少要有 email 才算有效缓存
    return !out.email.empty();
}

bool RedisDao::InvalidateUserCache(redisContext* ctx, uint32_t user_id) {
    if (!ctx) return false;

    std::string key = MakeUserInfoKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// ============================================================
// 邮箱 → 用户 ID 映射（String）
// ============================================================

bool RedisDao::CacheEmailMapping(redisContext* ctx, const std::string& email, uint32_t user_id) {
    if (!ctx) return false;

    std::string key = MakeEmailKey(email);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %u", key.c_str(), user_id);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisDao::GetUserIdByEmail(redisContext* ctx, const std::string& email, uint32_t& out_id) {
    if (!ctx) return false;

    std::string key = MakeEmailKey(email);
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());

    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    out_id = static_cast<uint32_t>(std::stoul(reply->str));
    freeReplyObject(reply);
    return true;
}

bool RedisDao::InvalidateEmailMapping(redisContext* ctx, const std::string& email) {
    if (!ctx) return false;

    std::string key = MakeEmailKey(email);
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// ============================================================
// 验证码（String + TTL）
// ============================================================

bool RedisDao::SetVerificationCode(redisContext* ctx, const std::string& email,
                                   int type, const std::string& code, int expire_seconds) {
    if (!ctx) return false;

    std::string key = MakeCodeKey(type, email);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SETEX %s %d %s",
        key.c_str(), expire_seconds, code.c_str());

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisDao::GetAndConsumeVerificationCode(redisContext* ctx, const std::string& email,
                                             int type, std::string& out_code) {
    if (!ctx) return false;

    std::string key = MakeCodeKey(type, email);

    // 1. GET 验证码
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return false;  // 不存在或已过期
    }

    out_code.assign(reply->str, reply->len);
    freeReplyObject(reply);

    // 2. 立即删除（消耗验证码，防止重放）
    reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    freeReplyObject(reply);  // 不管 DEL 结果了

    return !out_code.empty();
}

// ============================================================
// 在线状态（String + TTL）
// ============================================================

bool RedisDao::SetUserOnline(redisContext* ctx, uint32_t user_id, int ttl_seconds) {
    if (!ctx) return false;

    std::string key = MakeOnlineKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SETEX %s %d 1",
        key.c_str(), ttl_seconds);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool RedisDao::IsUserOnline(redisContext* ctx, uint32_t user_id) {
    if (!ctx) return false;

    std::string key = MakeOnlineKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "EXISTS %s", key.c_str());

    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    bool online = (reply->integer == 1);
    freeReplyObject(reply);
    return online;
}

bool RedisDao::SetUserOffline(redisContext* ctx, uint32_t user_id) {
    if (!ctx) return false;

    std::string key = MakeOnlineKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// ============================================================
// 好友列表缓存（Set）
// ============================================================

bool RedisDao::GetFriendIdSet(redisContext* ctx, uint32_t user_id,
                              std::vector<uint32_t>& out_friend_ids) {
    if (!ctx) return false;

    std::string key = MakeFriendListKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SMEMBERS %s", key.c_str());

    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    for (size_t i = 0; i < reply->elements; ++i) {
        uint32_t fid = static_cast<uint32_t>(std::stoul(reply->element[i]->str));
        out_friend_ids.push_back(fid);
    }
    freeReplyObject(reply);

    // 空集合也算成功（可能是没有好友）
    return true;
}

bool RedisDao::CacheFriendIdSet(redisContext* ctx, uint32_t user_id,
                                const std::vector<uint32_t>& friend_ids, int ttl_seconds) {
    if (!ctx) return false;

    std::string key = MakeFriendListKey(user_id);

    // 先删除旧集合
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply) freeReplyObject(reply);

    if (friend_ids.empty()) {
        return true;  // 空好友列表
    }

    // 逐个 SADD（简单可靠，好友数量通常不大）
    for (auto fid : friend_ids) {
        reply = (redisReply*)redisCommand(ctx, "SADD %s %u", key.c_str(), fid);
        if (reply) freeReplyObject(reply);
    }

    // 设置过期时间
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s %d", key.c_str(), ttl_seconds);
    if (reply) freeReplyObject(reply);

    return true;
}

bool RedisDao::AddFriendToSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id) {
    if (!ctx) return false;

    std::string key = MakeFriendListKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SADD %s %u", key.c_str(), friend_id);
    if (reply) freeReplyObject(reply);
    return true;
}

bool RedisDao::RemoveFriendFromSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id) {
    if (!ctx) return false;

    std::string key = MakeFriendListKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SREM %s %u", key.c_str(), friend_id);
    if (reply) freeReplyObject(reply);
    return true;
}

bool RedisDao::IsInFriendSet(redisContext* ctx, uint32_t user_id, uint32_t friend_id) {
    if (!ctx) return false;

    std::string key = MakeFriendListKey(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "SISMEMBER %s %u", key.c_str(), friend_id);

    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    bool result = (reply->integer == 1);
    freeReplyObject(reply);
    return result;
}

// ============================================================
// 搜索历史（List）
// ============================================================

bool RedisDao::AddSearchHistory(redisContext* ctx, uint32_t user_id,
                                const std::string& keyword) {
    if (!ctx || keyword.empty()) return false;

    std::string key = "search:history:" + std::to_string(user_id);

    // 先移除重复项（避免同一关键词重复出现）
    redisReply* reply = (redisReply*)redisCommand(ctx, "LREM %s 0 %s",
        key.c_str(), keyword.c_str());
    if (reply) freeReplyObject(reply);

    // LPUSH 到头部
    reply = (redisReply*)redisCommand(ctx, "LPUSH %s %s",
        key.c_str(), keyword.c_str());
    if (reply) freeReplyObject(reply);

    // 保留最近 6 条
    reply = (redisReply*)redisCommand(ctx, "LTRIM %s 0 5", key.c_str());
    if (reply) freeReplyObject(reply);

    return true;
}

bool RedisDao::GetSearchHistory(redisContext* ctx, uint32_t user_id,
                                std::vector<std::string>& out) {
    if (!ctx) return false;

    std::string key = "search:history:" + std::to_string(user_id);
    redisReply* reply = (redisReply*)redisCommand(ctx, "LRANGE %s 0 5", key.c_str());

    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return false;
    }

    for (size_t i = 0; i < reply->elements; ++i) {
        out.push_back(std::string(reply->element[i]->str, reply->element[i]->len));
    }
    freeReplyObject(reply);
    return true;
}

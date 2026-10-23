#include "cache/RedisDao.h"

#include <iterator>
#include <unordered_map>

namespace {

// 统一生成各类缓存键，避免业务代码重复拼接字符串。
std::string UserKey(uint32_t user_id) {
    return std::to_string(user_id);
}

std::string UserInfoKey(uint32_t user_id) {
    return "user:info:" + UserKey(user_id);
}

std::string UserEmailKey(const std::string& email) {
    return "user:email:" + email;
}

std::string VerificationCodeKey(int type, const std::string& email) {
    return "code:" + std::to_string(type) + ":" + email;
}

std::string OnlineKey(uint32_t user_id) {
    return "online:" + UserKey(user_id);
}

std::string FriendListKey(uint32_t user_id) {
    return "friend:list:" + UserKey(user_id);
}

std::string SearchHistoryKey(uint32_t user_id) {
    return "search:history:" + UserKey(user_id);
}

}  // namespace

bool RedisDao::CacheUserInfo(sw::redis::Redis* redis, const UserInfo& user) {
    if (!redis) {
        return false;
    }

    try {
        const auto cache_key = UserInfoKey(user.id);
        redis->hset(cache_key, "email", user.email);
        redis->hset(cache_key, "username", user.username);
        redis->hset(cache_key, "password", user.password);
        redis->hset(cache_key, "salt", user.salt);
        redis->hset(cache_key, "avatar", user.avatar);
        redis->hset(cache_key, "created_at", user.created_at);
        return true;
    } catch (const std::exception&) {
        // Redis 异常时返回失败，由业务层决定是否访问数据库。
        return false;
    }
}

bool RedisDao::GetCachedUserInfo(sw::redis::Redis* redis,
                                 uint32_t user_id,
                                 UserInfo& user) {
    if (!redis) {
        return false;
    }

    try {
        std::unordered_map<std::string, std::string> values;
        redis->hgetall(UserInfoKey(user_id), std::inserter(values, values.end()));
        if (values.empty()) {
            return false;
        }

        user.id = user_id;
        user.email = values["email"];
        user.username = values["username"];
        user.password = values["password"];
        user.salt = values["salt"];
        user.avatar = values["avatar"];
        user.created_at = values["created_at"];
        return !user.email.empty();
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::InvalidateUserCache(sw::redis::Redis* redis, uint32_t user_id) {
    if (!redis) {
        return false;
    }

    try {
        redis->del(UserInfoKey(user_id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::CacheEmailMapping(sw::redis::Redis* redis,
                                 const std::string& email,
                                 uint32_t user_id) {
    if (!redis) {
        return false;
    }

    try {
        redis->set(UserEmailKey(email), UserKey(user_id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::GetUserIdByEmail(sw::redis::Redis* redis,
                                const std::string& email,
                                uint32_t& user_id) {
    if (!redis) {
        return false;
    }

    try {
        auto value = redis->get(UserEmailKey(email));
        if (!value) {
            return false;
        }

        user_id = std::stoul(*value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::InvalidateEmailMapping(sw::redis::Redis* redis,
                                      const std::string& email) {
    if (!redis) {
        return false;
    }

    try {
        redis->del(UserEmailKey(email));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::SetVerificationCode(sw::redis::Redis* redis,
                                   const std::string& email,
                                   int type,
                                   const std::string& code,
                                   int ttl_seconds) {
    if (!redis) {
        return false;
    }

    try {
        redis->setex(VerificationCodeKey(type, email), ttl_seconds, code);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::GetAndConsumeVerificationCode(sw::redis::Redis* redis,
                                              const std::string& email,
                                              int type,
                                              std::string& code) {
    if (!redis) {
        return false;
    }

    try {
        auto value = redis->get(VerificationCodeKey(type, email));
        if (!value) {
            return false;
        }

        code = *value;
        redis->del(VerificationCodeKey(type, email));
        return !code.empty();
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::SetUserOnline(sw::redis::Redis* redis,
                             uint32_t user_id,
                             int ttl_seconds) {
    if (!redis) {
        return false;
    }

    try {
        redis->setex(OnlineKey(user_id), ttl_seconds, "1");
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::IsUserOnline(sw::redis::Redis* redis, uint32_t user_id) {
    if (!redis) {
        return false;
    }

    try {
        return redis->exists(OnlineKey(user_id)) > 0;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::SetUserOffline(sw::redis::Redis* redis, uint32_t user_id) {
    if (!redis) {
        return false;
    }

    try {
        redis->del(OnlineKey(user_id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::GetFriendIdSet(sw::redis::Redis* redis,
                              uint32_t user_id,
                              std::vector<uint32_t>& friend_ids) {
    if (!redis) {
        return false;
    }

    try {
        std::vector<std::string> values;
        redis->smembers(FriendListKey(user_id), std::back_inserter(values));
        for (const auto& value : values) {
            friend_ids.push_back(std::stoul(value));
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::CacheFriendIdSet(sw::redis::Redis* redis,
                                uint32_t user_id,
                                const std::vector<uint32_t>& friend_ids,
                                int ttl_seconds) {
    if (!redis) {
        return false;
    }

    try {
        const auto cache_key = FriendListKey(user_id);
        redis->del(cache_key);
        for (uint32_t friend_id : friend_ids) {
            redis->sadd(cache_key, UserKey(friend_id));
        }
        if (!friend_ids.empty()) {
            redis->expire(cache_key, ttl_seconds);
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::AddFriendToSet(sw::redis::Redis* redis,
                              uint32_t user_id,
                              uint32_t friend_id) {
    if (!redis) {
        return false;
    }

    try {
        redis->sadd(FriendListKey(user_id), UserKey(friend_id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::RemoveFriendFromSet(sw::redis::Redis* redis,
                                   uint32_t user_id,
                                   uint32_t friend_id) {
    if (!redis) {
        return false;
    }

    try {
        redis->srem(FriendListKey(user_id), UserKey(friend_id));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::IsInFriendSet(sw::redis::Redis* redis,
                             uint32_t user_id,
                             uint32_t friend_id) {
    if (!redis) {
        return false;
    }

    try {
        return redis->sismember(FriendListKey(user_id), UserKey(friend_id));
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::AddSearchHistory(sw::redis::Redis* redis,
                                uint32_t user_id,
                                const std::string& keyword) {
    if (!redis || keyword.empty()) {
        return false;
    }

    try {
        const auto cache_key = SearchHistoryKey(user_id);
        redis->lrem(cache_key, 0, keyword);
        redis->lpush(cache_key, keyword);
        redis->ltrim(cache_key, 0, 5);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool RedisDao::GetSearchHistory(sw::redis::Redis* redis,
                                uint32_t user_id,
                                std::vector<std::string>& history) {
    if (!redis) {
        return false;
    }

    try {
        redis->lrange(SearchHistoryKey(user_id), 0, 5, std::back_inserter(history));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

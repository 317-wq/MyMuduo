#pragma once

/*
    用户业务逻辑层

    负责协调注册 / 登录 / 验证码流程：
    - 注册：检查邮箱 → 哈希密码 → 插入用户 → 发验证码
    - 登录：查用户 → 验密码
    - 所有 DB 操作通过 Database::Execute 异步执行，回调回到 io_loop
    - 支持 Redis 缓存加速（可选），启用后验证码存 Redis、用户信息 Cache-Aside

    callback 约定：
    - error_code: 0 = 成功，非 0 = 失败
    - msg: 错误描述或成功提示
*/

#include "db/Database.h"
#include "db/UserDao.h"
#include "cache/RedisCache.h"
#include "cache/RedisDao.h"
#include "service/EmailSender.h"
#include "net/EventLoop.h"

#include <functional>
#include <string>
#include <cstdint>

class UserService : NoCopy {
public:
    // 回调类型定义
    using RegisterCallback = std::function<void(int error_code,
                                                const std::string& msg,
                                                uint32_t user_id)>;
    using LoginCallback = std::function<void(int error_code,
                                             const std::string& msg,
                                             const UserInfo& user)>;
    using SimpleCallback = std::function<void(int error_code,
                                              const std::string& msg)>;

    UserService(Database* db, EventLoop* loop)
        : _db(db), _loop(loop) {}

    // 设置 Redis 缓存（可选，不设置则原有 MySQL 直连行为不变）
    void SetRedisCache(RedisCache* redis) { _redis = redis; }

    // 设置邮件配置（需要在发送验证码前调用）
    void SetEmailConfig(const EmailSender::Config& cfg) { _email_cfg = cfg; }

    // ---------- 注册流程 ----------

    // 第一步：发送邮箱验证码
    // 优先使用 Redis（带 TTL），Redis 不可用时回退 MySQL
    void SendVerificationCode(const std::string& email, int type,
                              SimpleCallback callback);

    // 第二步：验证验证码 + 完成注册
    // 优先从 Redis 验证，Redis 不可用时回退 MySQL
    void RegisterWithCode(const std::string& email,
                          const std::string& code,
                          const std::string& password,
                          const std::string& username,
                          RegisterCallback callback);

    // ---------- 登录流程 ----------

    // 邮箱 + 密码登录（Cache-Aside：先 Redis 后 MySQL）
    void Login(const std::string& email,
               const std::string& password,
               LoginCallback callback);

private:
    // 纯 MySQL 路径（Redis 不可用时的回退）
    void SendCodeViaMySQL(const std::string& email, int type,
                          SimpleCallback callback);
    void RegisterViaMySQL(const std::string& email, const std::string& code,
                          const std::string& password, const std::string& username,
                          RegisterCallback callback);
    void LoginViaMySQL(const std::string& email, const std::string& password,
                       LoginCallback callback);

    // 发送验证码邮件（在 DB/Redis worker 线程调用）
    static void TrySendEmail(const EmailSender::Config& cfg,
                             const std::string& email, const std::string& code);

    Database* _db;
    EventLoop* _loop;
    RedisCache* _redis = nullptr;
    EmailSender::Config _email_cfg;
};

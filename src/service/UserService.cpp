#include "service/UserService.h"
#include "base/Crypto.h"
#include "net/Log.h"

#include <string>
#include <memory>

// ============================================================
// 发送验证码邮件（工具函数，在 worker 线程调用）
// ============================================================

void UserService::TrySendEmail(const EmailSender::Config& cfg,
                               const std::string& email, const std::string& code)
{
    if (!cfg.smtp_host.empty() && !cfg.smtp_user.empty()) {
        EmailSender::SendVerificationCode(cfg, email, code, 5);
    }
}

// ============================================================
// 发送验证码（优先 Redis）
// ============================================================

void UserService::SendVerificationCode(const std::string& email,
                                       int type,
                                       SimpleCallback callback)
{
    if (!_redis) {
        SendCodeViaMySQL(email, type, std::move(callback));
        return;
    }

    std::string code = Crypto::GenerateVerificationCode();
    EmailSender::Config email_cfg = _email_cfg;

    struct Result {
        int error_code = 0;
        std::string msg;
    };
    auto result = std::make_shared<Result>();

    _redis->Execute(_loop,
        // --- Redis 线程 ---
        [email, code, type, email_cfg, result](redisContext* ctx) {
            if (!RedisDao::SetVerificationCode(ctx, email, type, code, 300)) {
                result->error_code = 1;
                result->msg = "验证码存储失败";
                return;
            }

            // 发送邮件（在 Redis worker 线程中同步发送）
            TrySendEmail(email_cfg, email, code);

            result->error_code = 0;
            result->msg = "验证码已发送";
        },
        // --- IO 线程 ---
        [email, code, email_cfg, result, callback = std::move(callback)]() {
            if (email_cfg.smtp_host.empty()) {
                // 未配置 SMTP，回退到日志输出（开发模式）
                LOG_INFO("Verification code for %s: %s", email.c_str(), code.c_str());
            }
            callback(result->error_code, result->msg);
        });
}

// ============================================================
// 发送验证码（纯 MySQL 回退路径）
// ============================================================

void UserService::SendCodeViaMySQL(const std::string& email,
                                   int type,
                                   SimpleCallback callback)
{
    std::string code = Crypto::GenerateVerificationCode();
    EmailSender::Config email_cfg = _email_cfg;

    struct Result {
        int error_code = 0;
        std::string msg;
    };
    auto result = std::make_shared<Result>();

    _db->Execute(_loop,
        [email, code, type, email_cfg, result](sql::Connection* conn) {
            if (!UserDao::InsertVerificationCode(conn, email, code, type, 300)) {
                result->error_code = 1;
                result->msg = "验证码入库失败";
                return;
            }
            TrySendEmail(email_cfg, email, code);
            result->error_code = 0;
            result->msg = "验证码已发送";
        },
        [email, code, email_cfg, result, callback = std::move(callback)]() {
            if (email_cfg.smtp_host.empty()) {
                LOG_INFO("Verification code for %s: %s", email.c_str(), code.c_str());
            }
            callback(result->error_code, result->msg);
        });
}

// ============================================================
// 验证码 + 注册（优先 Redis）
// ============================================================

void UserService::RegisterWithCode(const std::string& email,
                                   const std::string& code,
                                   const std::string& password,
                                   const std::string& username,
                                   RegisterCallback callback)
{
    if (!_redis) {
        RegisterViaMySQL(email, code, password, username, std::move(callback));
        return;
    }

    struct Result {
        int error_code = 0;
        std::string msg;
        uint32_t user_id = 0;
    };
    auto result = std::make_shared<Result>();

    // Step 1: 从 Redis 验证验证码
    _redis->Execute(_loop,
        [email, code, result](redisContext* ctx) {
            std::string stored_code;
            if (!RedisDao::GetAndConsumeVerificationCode(ctx, email, 1, stored_code)) {
                result->error_code = 1;
                result->msg = "验证码错误或已过期";
                return;
            }
            if (stored_code != code) {
                result->error_code = 1;
                result->msg = "验证码错误";
                return;
            }
            result->error_code = 0;  // 验证码通过
        },
        // Step 2: 验证码通过 → MySQL 插入用户
        [this, email, code, password, username, result,
         callback = std::move(callback)]() mutable
        {
            if (result->error_code != 0) {
                callback(result->error_code, result->msg, 0);
                return;
            }

            _db->Execute(_loop,
                [email, password, username, result](sql::Connection* conn) {
                    // 检查邮箱是否已注册
                    if (UserDao::EmailExists(conn, email)) {
                        result->error_code = 2;
                        result->msg = "该邮箱已被注册";
                        return;
                    }

                    // 哈希密码
                    std::string salt = Crypto::GenerateSalt();
                    std::string hash = Crypto::HashPassword(password, salt);

                    // 插入用户
                    uint32_t user_id = 0;
                    if (!UserDao::InsertUser(conn, email, hash, salt, username, user_id)) {
                        result->error_code = 3;
                        result->msg = "创建用户失败";
                        return;
                    }

                    result->error_code = 0;
                    result->msg = "注册成功";
                    result->user_id = user_id;
                },
                // Step 3: MySQL 成功 → 回填 Redis 缓存
                [this, email, result, callback = std::move(callback)]() {
                    if (result->error_code == 0 && _redis) {
                        // 查询 UserInfo 用于缓存（需要再次查 MySQL）
                        // 简化：这里不做额外查询，注册完成后 Login 会自动缓存
                        // 但可以先缓存 email → id 映射
                        _redis->Execute(nullptr,
                            [email, user_id = result->user_id](redisContext* ctx) {
                                RedisDao::CacheEmailMapping(ctx, email, user_id);
                            },
                            nullptr);
                    }
                    callback(result->error_code, result->msg, result->user_id);
                });
        });
}

// ============================================================
// 注册（纯 MySQL 回退路径）
// ============================================================

void UserService::RegisterViaMySQL(const std::string& email,
                                   const std::string& code,
                                   const std::string& password,
                                   const std::string& username,
                                   RegisterCallback callback)
{
    struct Result {
        int error_code = 0;
        std::string msg;
        uint32_t user_id = 0;
    };
    auto result = std::make_shared<Result>();

    _db->Execute(_loop,
        [email, code, password, username, result](sql::Connection* conn) {
            // 1. 验证验证码
            if (!UserDao::VerifyCode(conn, email, code, 1)) {
                result->error_code = 1;
                result->msg = "验证码错误或已过期";
                return;
            }

            // 2. 检查邮箱是否已注册
            if (UserDao::EmailExists(conn, email)) {
                result->error_code = 2;
                result->msg = "该邮箱已被注册";
                return;
            }

            // 3. 哈希密码
            std::string salt = Crypto::GenerateSalt();
            std::string hash = Crypto::HashPassword(password, salt);

            // 4. 插入用户
            uint32_t user_id = 0;
            if (!UserDao::InsertUser(conn, email, hash, salt, username, user_id)) {
                result->error_code = 3;
                result->msg = "创建用户失败";
                return;
            }

            result->error_code = 0;
            result->msg = "注册成功";
            result->user_id = user_id;
        },
        [result, callback = std::move(callback)]() {
            callback(result->error_code, result->msg, result->user_id);
        });
}

// ============================================================
// 登录（Cache-Aside 模式）
// ============================================================

void UserService::Login(const std::string& email,
                        const std::string& password,
                        LoginCallback callback)
{
    if (!_redis) {
        LoginViaMySQL(email, password, std::move(callback));
        return;
    }

    auto user = std::make_shared<UserInfo>();
    struct LoginResult {
        int error_code = 0;
        std::string msg;
        bool cache_hit = false;
    };
    auto login_result = std::make_shared<LoginResult>();
    EventLoop* loop = _loop;
    Database* db = _db;

    // Step 1: 尝试从 Redis 缓存获取
    _redis->Execute(loop,
        [email, user, login_result](redisContext* ctx) {
            uint32_t uid = 0;
            if (RedisDao::GetUserIdByEmail(ctx, email, uid) &&
                RedisDao::GetCachedUserInfo(ctx, uid, *user)) {
                login_result->cache_hit = true;
                return;
            }
            login_result->cache_hit = false;
        },
        [this, email, password, user, login_result,
         callback = std::move(callback), loop, db]() mutable
        {
            if (login_result->cache_hit) {
                // 缓存命中 → 直接验密码
                if (Crypto::VerifyPassword(password, user->salt, user->password)) {
                    login_result->error_code = 0;
                    login_result->msg = "登录成功";
                } else {
                    login_result->error_code = 2;
                    login_result->msg = "密码错误";
                }
                callback(login_result->error_code, login_result->msg, *user);
                return;
            }

            // Step 2: 缓存未命中 → 查 MySQL
            db->Execute(loop,
                [email, password, user, login_result](sql::Connection* conn) {
                    if (!UserDao::GetUserByEmail(conn, email, *user)) {
                        login_result->error_code = 1;
                        login_result->msg = "该邮箱未注册";
                        return;
                    }
                    if (!Crypto::VerifyPassword(password, user->salt, user->password)) {
                        login_result->error_code = 2;
                        login_result->msg = "密码错误";
                        return;
                    }
                    login_result->error_code = 0;
                    login_result->msg = "登录成功";
                },
                // Step 3: MySQL 成功 → 回填 Redis（fire-and-forget）
                [this, user, login_result, callback = std::move(callback)]() {
                    if (login_result->error_code == 0 && _redis) {
                        _redis->Execute(nullptr,
                            [user](redisContext* ctx) {
                                RedisDao::CacheUserInfo(ctx, *user);
                                RedisDao::CacheEmailMapping(ctx, user->email, user->id);
                            },
                            nullptr);
                    }
                    callback(login_result->error_code, login_result->msg, *user);
                });
        });
}

// ============================================================
// 登录（纯 MySQL 回退路径）
// ============================================================

void UserService::LoginViaMySQL(const std::string& email,
                                const std::string& password,
                                LoginCallback callback)
{
    auto user = std::make_shared<UserInfo>();
    struct LoginResult {
        int error_code = 0;
        std::string msg;
    };
    auto login_result = std::make_shared<LoginResult>();

    _db->Execute(_loop,
        [email, password, user, login_result](sql::Connection* conn) {
            if (!UserDao::GetUserByEmail(conn, email, *user)) {
                login_result->error_code = 1;
                login_result->msg = "该邮箱未注册";
                return;
            }
            if (!Crypto::VerifyPassword(password, user->salt, user->password)) {
                login_result->error_code = 2;
                login_result->msg = "密码错误";
                return;
            }
            login_result->error_code = 0;
            login_result->msg = "登录成功";
        },
        [user, login_result, callback = std::move(callback)]() {
            callback(login_result->error_code, login_result->msg, *user);
        });
}

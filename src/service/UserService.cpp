#include "service/UserService.h"
#include "base/Crypto.h"
#include "net/Log.h"

#include <string>
#include <memory>

// ============================================================
// 发送验证码
// ============================================================

void UserService::SendVerificationCode(const std::string& email,
                                       int type,
                                       SimpleCallback callback)
{
    std::string code = Crypto::GenerateVerificationCode();

    // 捕获邮件配置副本，在 DB 线程中发送邮件
    EmailSender::Config email_cfg = _email_cfg;

    struct Result {
        int error_code = 0;
        std::string msg;
    };
    auto result = std::make_shared<Result>();

    _db->Execute(_loop,
        // --- DB 线程 ---
        [email, code, type, email_cfg, result](sql::Connection* conn) {
            // 1. 验证码入库
            if (!UserDao::InsertVerificationCode(conn, email, code, type, 300)) {
                result->error_code = 1;
                result->msg = "验证码入库失败";
                return;
            }

            // 2. 发送邮件
            if (!email_cfg.smtp_host.empty() && !email_cfg.smtp_user.empty()) {
                if (!EmailSender::SendVerificationCode(email_cfg, email, code, 5)) {
                    // 邮件发送失败，但验证码已入库（用户可能通过其他方式获知）
                    // 这里只记录失败，不回滚
                    result->error_code = 0;
                    result->msg = "验证码已生成（邮件发送失败，请联系管理员）";
                    return;
                }
            }

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
// 验证码 + 注册
// ============================================================

void UserService::RegisterWithCode(const std::string& email,
                                   const std::string& code,
                                   const std::string& password,
                                   const std::string& username,
                                   RegisterCallback callback)
{
    // shared_ptr 在 DB 线程和 IO 线程之间传递结果
    struct Result {
        int error_code = 0;
        std::string msg;
        uint32_t user_id = 0;
    };
    auto result = std::make_shared<Result>();

    _db->Execute(_loop,
        // --- DB 线程 ---
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
        // --- IO 线程 ---
        [result, callback = std::move(callback)]() {
            callback(result->error_code, result->msg, result->user_id);
        });
}

// ============================================================
// 登录
// ============================================================

void UserService::Login(const std::string& email,
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
        // --- DB 线程 ---
        [email, password, user, login_result](sql::Connection* conn) {
            // 1. 查用户
            if (!UserDao::GetUserByEmail(conn, email, *user)) {
                login_result->error_code = 1;
                login_result->msg = "该邮箱未注册";
                return;
            }

            // 2. 验密码
            if (!Crypto::VerifyPassword(password, user->salt, user->password)) {
                login_result->error_code = 2;
                login_result->msg = "密码错误";
                return;
            }

            login_result->error_code = 0;
            login_result->msg = "登录成功";
        },
        // --- IO 线程 ---
        [user, login_result, callback = std::move(callback)]() {
            callback(login_result->error_code, login_result->msg, *user);
        });
}

#pragma once

/*
    用户业务逻辑层

    负责协调注册 / 登录 / 验证码流程：
    - 注册：检查邮箱 → 哈希密码 → 插入用户 → 发验证码
    - 登录：查用户 → 验密码
    - 所有 DB 操作通过 Database::Execute 异步执行，回调回到 io_loop

    callback 约定：
    - error_code: 0 = 成功，非 0 = 失败
    - msg: 错误描述或成功提示
*/

#include "db/Database.h"
#include "db/UserDao.h"
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

    // 设置邮件配置（需要在发送验证码前调用）
    void SetEmailConfig(const EmailSender::Config& cfg) { _email_cfg = cfg; }

    // ---------- 注册流程 ----------

    // 第一步：发送邮箱验证码
    // 验证码生成 → 入库 → SMTP 发送邮件
    void SendVerificationCode(const std::string& email, int type,
                              SimpleCallback callback);

    // 第二步：验证验证码 + 完成注册
    // 验证通过后创建用户（哈希密码、写入 users 表）
    void RegisterWithCode(const std::string& email,
                          const std::string& code,
                          const std::string& password,
                          const std::string& username,
                          RegisterCallback callback);

    // ---------- 登录流程 ----------

    // 邮箱 + 密码登录
    void Login(const std::string& email,
               const std::string& password,
               LoginCallback callback);

private:
    Database* _db;
    EventLoop* _loop;
    EmailSender::Config _email_cfg;
};

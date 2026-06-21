#pragma once

/*
    用户数据访问对象 (Data Access Object)

    所有方法都是同步的，接收 sql::Connection*，
    由调用者通过 Database::Execute 在 DB 线程执行。

    使用 Prepared Statement 防 SQL 注入。
*/

#include <cppconn/connection.h>

#include <cstdint>
#include <string>

// 用户信息结构体
struct UserInfo {
    uint32_t    id = 0;
    std::string email;
    std::string password;    // 密码哈希 (hex)
    std::string salt;        // 随机盐 (hex)
    std::string username;
    std::string avatar;
    std::string created_at;
    std::string updated_at;
};

class UserDao {
public:
    // 插入新用户，返回自增 id 通过 out_id 传出
    static bool InsertUser(sql::Connection* conn,
                           const std::string& email,
                           const std::string& password_hash,
                           const std::string& salt,
                           const std::string& username,
                           uint32_t& out_id);

    // 根据邮箱查找用户
    static bool GetUserByEmail(sql::Connection* conn,
                               const std::string& email,
                               UserInfo& out_user);

    // 根据 id 查找用户
    static bool GetUserById(sql::Connection* conn,
                            uint32_t id,
                            UserInfo& out_user);

    // 检查邮箱是否已注册
    static bool EmailExists(sql::Connection* conn,
                            const std::string& email);

    // 更新头像路径
    static bool UpdateAvatar(sql::Connection* conn,
                             uint32_t user_id,
                             const std::string& avatar_path);

    // 插入验证码（type: 1=注册, 2=重置密码）
    // expire_seconds: 多少秒后过期
    static bool InsertVerificationCode(sql::Connection* conn,
                                       const std::string& email,
                                       const std::string& code,
                                       int type,
                                       int expire_seconds);

    // 验证验证码是否有效（检查匹配 + 未过期 + 未使用），验证通过后标记已使用
    static bool VerifyCode(sql::Connection* conn,
                           const std::string& email,
                           const std::string& code,
                           int type);
};

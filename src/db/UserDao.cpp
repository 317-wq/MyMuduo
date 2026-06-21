#include "db/UserDao.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <memory>

// 用户 CRUD

bool UserDao::InsertUser(sql::Connection* conn,
                         const std::string& email,
                         const std::string& password_hash,
                         const std::string& salt,
                         const std::string& username,
                         uint32_t& out_id)
{
    // 预编译，防止sql注入
    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
            "INSERT INTO users (email, password, salt, username) VALUES (?, ?, ?, ?)"));

    // 绑定类型
    stmt->setString(1, email);
    stmt->setString(2, password_hash);
    stmt->setString(3, salt);
    stmt->setString(4, username);
    stmt->executeUpdate();

    // 获取自增 id
    std::unique_ptr<sql::Statement> id_stmt(conn->createStatement());
    std::unique_ptr<sql::ResultSet> rs(id_stmt->executeQuery("SELECT LAST_INSERT_ID()"));
    if (rs->next()) {
        out_id = rs->getUInt(1);
        return true;
    }
    return false;
}

bool UserDao::GetUserByEmail(sql::Connection* conn,
                             const std::string& email,
                             UserInfo& out_user)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT id, email, password, salt, username, "
            "IFNULL(avatar, '') AS avatar, created_at, updated_at "
            "FROM users WHERE email = ?"));

    stmt->setString(1, email);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        out_user.id         = rs->getUInt("id");
        out_user.email      = rs->getString("email");
        out_user.password   = rs->getString("password");
        out_user.salt       = rs->getString("salt");
        out_user.username   = rs->getString("username");
        out_user.avatar     = rs->getString("avatar");
        out_user.created_at = rs->getString("created_at");
        out_user.updated_at = rs->getString("updated_at");
        return true;
    }
    return false;
}

bool UserDao::GetUserById(sql::Connection* conn,
                          uint32_t id,
                          UserInfo& out_user)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT id, email, password, salt, username, "
            "IFNULL(avatar, '') AS avatar, created_at, updated_at "
            "FROM users WHERE id = ?"));

    stmt->setUInt(1, id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        out_user.id         = rs->getUInt("id");
        out_user.email      = rs->getString("email");
        out_user.password   = rs->getString("password");
        out_user.salt       = rs->getString("salt");
        out_user.username   = rs->getString("username");
        out_user.avatar     = rs->getString("avatar");
        out_user.created_at = rs->getString("created_at");
        out_user.updated_at = rs->getString("updated_at");
        return true;
    }
    return false;
}

bool UserDao::EmailExists(sql::Connection* conn,
                          const std::string& email)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("SELECT COUNT(*) FROM users WHERE email = ?"));

    stmt->setString(1, email);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        return rs->getInt(1) > 0;
    }
    return false;
}

bool UserDao::UpdateAvatar(sql::Connection* conn,
                           uint32_t user_id,
                           const std::string& avatar_path)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement("UPDATE users SET avatar = ? WHERE id = ?"));

    stmt->setString(1, avatar_path);
    stmt->setUInt(2, user_id);
    return stmt->executeUpdate() > 0;
}

// ============================================================
// 验证码
// ============================================================

bool UserDao::InsertVerificationCode(sql::Connection* conn,
                                     const std::string& email,
                                     const std::string& code,
                                     int type,
                                     int expire_seconds)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "INSERT INTO verification_codes (email, code, type, expires_at) "
            "VALUES (?, ?, ?, DATE_ADD(NOW(), INTERVAL ? SECOND))"));

    stmt->setString(1, email);
    stmt->setString(2, code);
    stmt->setInt(3, type);
    stmt->setInt(4, expire_seconds);
    return stmt->executeUpdate() > 0;
}

bool UserDao::VerifyCode(sql::Connection* conn,
                         const std::string& email,
                         const std::string& code,
                         int type)
{
    // 查找有效验证码
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT id FROM verification_codes "
            "WHERE email = ? AND code = ? AND type = ? "
            "AND used = 0 AND expires_at > NOW() "
            "ORDER BY id DESC LIMIT 1"));

    stmt->setString(1, email);
    stmt->setString(2, code);
    stmt->setInt(3, type);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (!rs->next()) {
        return false;   // 未找到有效验证码
    }

    int code_id = rs->getInt("id");

    // 标记为已使用
    std::unique_ptr<sql::PreparedStatement> update_stmt(
        conn->prepareStatement("UPDATE verification_codes SET used = 1 WHERE id = ?"));
    update_stmt->setInt(1, code_id);
    update_stmt->executeUpdate();

    return true;
}

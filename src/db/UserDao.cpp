#include "db/UserDao.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <memory>
#include <vector>

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
// 用户档案
// ============================================================

bool UserDao::GetUserProfile(sql::Connection* conn,
                             uint32_t user_id,
                             UserProfile& out)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT id, email, username, IFNULL(avatar, '') AS avatar, "
            "gender, IFNULL(birthday, '') AS birthday, "
            "IFNULL(secondary_email, '') AS secondary_email, created_at "
            "FROM users WHERE id = ?"));

    stmt->setUInt(1, user_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        out.id              = rs->getUInt("id");
        out.email           = rs->getString("email");
        out.username        = rs->getString("username");
        out.avatar          = rs->getString("avatar");
        out.gender          = rs->getInt("gender");
        out.birthday        = rs->getString("birthday");
        out.secondary_email = rs->getString("secondary_email");
        out.created_at      = rs->getString("created_at");
        return true;
    }
    return false;
}

bool UserDao::UpdateProfile(sql::Connection* conn,
                            uint32_t user_id,
                            const std::string& username,
                            int gender,
                            const std::string& birthday,
                            const std::string& secondary_email)
{
    // 动态构建 SQL，只更新有值的字段
    std::string sql = "UPDATE users SET ";
    std::vector<std::string> sets;
    std::vector<std::string> values;

    if (!username.empty()) {
        sets.push_back("username = ?");
        values.push_back(username);
    }
    if (gender >= 0 && gender <= 2) {
        sets.push_back("gender = ?");
        values.push_back(std::to_string(gender));
    }
    if (!birthday.empty()) {
        sets.push_back("birthday = ?");
        values.push_back(birthday);
    }
    // secondary_email 允许设为空字符串清空
    sets.push_back("secondary_email = ?");
    values.push_back(secondary_email);

    if (sets.empty()) return false;

    for (size_t i = 0; i < sets.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += sets[i];
    }
    sql += " WHERE id = ?";

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
    for (size_t i = 0; i < values.size(); ++i) {
        if (sets[i].find("gender") != std::string::npos) {
            stmt->setInt(i + 1, std::stoi(values[i]));
        } else {
            stmt->setString(i + 1, values[i]);
        }
    }
    stmt->setUInt(values.size() + 1, user_id);
    return stmt->executeUpdate() > 0;
}

bool UserDao::DeleteUser(sql::Connection* conn, uint32_t user_id)
{
    // 级联删除：私聊消息 → 好友关系 → 验证码 → 用户
    // 注意：MySQL 不支持一条语句多表删除，需要逐表清理

    bool was_auto_commit = true;
    try {
        conn->setAutoCommit(false);
        was_auto_commit = false;

        // 1. 删除与该用户相关的私聊消息
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "DELETE FROM private_messages WHERE from_user_id = ? OR to_user_id = ?"));
            stmt->setUInt(1, user_id);
            stmt->setUInt(2, user_id);
            stmt->executeUpdate();
        }

        // 2. 删除所有好友关系
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "DELETE FROM friends WHERE user_id = ? OR friend_id = ?"));
            stmt->setUInt(1, user_id);
            stmt->setUInt(2, user_id);
            stmt->executeUpdate();
        }

        // 3. 删除验证码
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "DELETE FROM verification_codes WHERE email = "
                    "(SELECT email FROM users WHERE id = ?)"));
            stmt->setUInt(1, user_id);
            stmt->executeUpdate();
        }

        // 4. 删除用户
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement("DELETE FROM users WHERE id = ?"));
            stmt->setUInt(1, user_id);
            if (stmt->executeUpdate() == 0) {
                conn->rollback();
                conn->setAutoCommit(true);
                return false;
            }
        }

        conn->commit();
        conn->setAutoCommit(true);
        return true;

    } catch (const sql::SQLException&) {
        if (!was_auto_commit) {
            conn->rollback();
            conn->setAutoCommit(true);
        }
        return false;
    }
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

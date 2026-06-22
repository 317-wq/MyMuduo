#include "db/FriendDao.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <memory>
#include <sstream>

// ============================================================
// 用户搜索
// ============================================================

bool FriendDao::SearchUserByEmail(sql::Connection* conn,
                                  const std::string& keyword,
                                  uint32_t exclude_user_id,
                                  std::vector<UserInfo>& out)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT id, email, username, IFNULL(avatar, '') AS avatar, created_at "
            "FROM users "
            "WHERE email LIKE ? AND id != ? "
            "LIMIT 20"));

    // 构造模糊匹配关键词（前后加 %）
    std::string pattern = "%" + keyword + "%";
    stmt->setString(1, pattern);
    stmt->setUInt(2, exclude_user_id);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
        UserInfo u;
        u.id         = rs->getUInt("id");
        u.email      = rs->getString("email");
        u.username   = rs->getString("username");
        u.avatar     = rs->getString("avatar");
        u.created_at = rs->getString("created_at");
        out.push_back(u);
    }
    return true;  // 空结果也算成功
}

// ============================================================
// 好友请求
// ============================================================

bool FriendDao::SendFriendRequest(sql::Connection* conn,
                                  uint32_t from_id, uint32_t to_id,
                                  uint32_t& out_request_id,
                                  bool& out_auto_accepted)
{
    out_auto_accepted = false;

    // 1. 检查是否已是好友
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT COUNT(*) FROM friends "
                "WHERE user_id = ? AND friend_id = ? AND status = 1"));
        stmt->setUInt(1, from_id);
        stmt->setUInt(2, to_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next() && rs->getInt(1) > 0) {
            return false;  // 已是好友
        }
    }

    // 2. 检查自己是否已发过待处理请求
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id FROM friends "
                "WHERE user_id = ? AND friend_id = ? AND status = 0"));
        stmt->setUInt(1, from_id);
        stmt->setUInt(2, to_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
            // 已发过请求，返回已有请求 ID
            out_request_id = rs->getUInt("id");
            return true;
        }
    }

    // 3. 检查对方是否已发来待处理请求（反向）
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT id FROM friends "
                "WHERE user_id = ? AND friend_id = ? AND status = 0"));
        stmt->setUInt(1, to_id);
        stmt->setUInt(2, from_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
            out_request_id = rs->getUInt("id");
            // 自动接受反向请求 → 双向好友
            uint32_t accepted_friend_id = 0;
            return AcceptFriendRequest(conn, from_id, out_request_id, accepted_friend_id);
        }
    }

    // 4. 正常插入新请求
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "INSERT INTO friends (user_id, friend_id, status) VALUES (?, ?, 0)"));
    stmt->setUInt(1, from_id);
    stmt->setUInt(2, to_id);

    try {
        stmt->executeUpdate();
    } catch (const sql::SQLException&) {
        // UNIQUE KEY 冲突（并发重复请求），忽略
        return false;
    }

    // 获取自增 ID
    std::unique_ptr<sql::Statement> id_stmt(conn->createStatement());
    std::unique_ptr<sql::ResultSet> rs(id_stmt->executeQuery("SELECT LAST_INSERT_ID()"));
    if (rs->next()) {
        out_request_id = rs->getUInt(1);
    }
    return true;
}

// ============================================================
// 同意好友请求
// ============================================================

bool FriendDao::AcceptFriendRequest(sql::Connection* conn,
                                    uint32_t user_id, uint32_t request_id,
                                    uint32_t& out_friend_id)
{
    // 1. 查询请求详情
    uint32_t from_id = 0;
    uint32_t to_id = 0;
    int status = -1;
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT user_id, friend_id, status FROM friends WHERE id = ?"));
        stmt->setUInt(1, request_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) return false;

        from_id = rs->getUInt("user_id");
        to_id   = rs->getUInt("friend_id");
        status  = rs->getInt("status");

        // 验证：请求的接受者必须是当前用户（friend_id == user_id）
        if (to_id != user_id) return false;
        // 只能接受 pending 状态的请求
        if (status != 0) return false;
    }

    // 2. 事务：更新原记录 + 插入反向记录
    bool was_auto_commit = true;
    try {
        // 检查自动提交状态（MySQL Connector/C++ 默认 auto_commit=true）
        conn->setAutoCommit(false);
        was_auto_commit = false;

        // 更新原记录为 accepted
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "UPDATE friends SET status = 1 WHERE id = ? AND status = 0"));
            stmt->setUInt(1, request_id);
            if (stmt->executeUpdate() == 0) {
                conn->rollback();
                conn->setAutoCommit(true);
                return false;
            }
        }

        // 插入反向记录（friend_id → user_id, status=1）
        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "INSERT INTO friends (user_id, friend_id, status) VALUES (?, ?, 1)"));
            stmt->setUInt(1, to_id);     // B
            stmt->setUInt(2, from_id);   // A
            stmt->executeUpdate();
        }

        conn->commit();
        conn->setAutoCommit(true);

        out_friend_id = from_id;
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
// 拒绝好友请求
// ============================================================

bool FriendDao::RejectFriendRequest(sql::Connection* conn,
                                    uint32_t user_id, uint32_t request_id)
{
    // 只有请求的接收者（friend_id == user_id）可以拒绝
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "DELETE FROM friends WHERE id = ? AND friend_id = ? AND status = 0"));
    stmt->setUInt(1, request_id);
    stmt->setUInt(2, user_id);
    return stmt->executeUpdate() > 0;
}

// ============================================================
// 好友管理
// ============================================================

bool FriendDao::DeleteFriend(sql::Connection* conn,
                             uint32_t user_id, uint32_t friend_id)
{
    // 双向删除（不管哪个方向，全部删掉）
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "DELETE FROM friends WHERE "
            "(user_id = ? AND friend_id = ?) OR (user_id = ? AND friend_id = ?)"));
    stmt->setUInt(1, user_id);
    stmt->setUInt(2, friend_id);
    stmt->setUInt(3, friend_id);
    stmt->setUInt(4, user_id);
    return stmt->executeUpdate() > 0;
}

bool FriendDao::GetFriendList(sql::Connection* conn,
                              uint32_t user_id,
                              std::vector<FriendInfo>& out)
{
    // JOIN users 表获取好友信息
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT u.id, u.email, u.username, IFNULL(u.avatar, '') AS avatar, "
            "IFNULL(f.remark, '') AS remark, f.created_at "
            "FROM friends f "
            "INNER JOIN users u ON f.friend_id = u.id "
            "WHERE f.user_id = ? AND f.status = 1 "
            "ORDER BY u.username ASC"));

    stmt->setUInt(1, user_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    while (rs->next()) {
        FriendInfo fi;
        fi.friend_id  = rs->getUInt("id");
        fi.email      = rs->getString("email");
        fi.username   = rs->getString("username");
        fi.avatar     = rs->getString("avatar");
        fi.remark     = rs->getString("remark");
        fi.created_at = rs->getString("created_at");
        out.push_back(fi);
    }
    return true;
}

bool FriendDao::GetPendingRequests(sql::Connection* conn,
                                   uint32_t user_id,
                                   std::vector<FriendRequest>& out)
{
    // JOIN users 表获取发送者信息
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT f.id, f.user_id, u.email, u.username, "
            "IFNULL(u.avatar, '') AS avatar, f.created_at "
            "FROM friends f "
            "INNER JOIN users u ON f.user_id = u.id "
            "WHERE f.friend_id = ? AND f.status = 0 "
            "ORDER BY f.created_at DESC"));

    stmt->setUInt(1, user_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    while (rs->next()) {
        FriendRequest fr;
        fr.id            = rs->getUInt("id");
        fr.from_user_id  = rs->getUInt("user_id");
        fr.from_email    = rs->getString("email");
        fr.from_username = rs->getString("username");
        fr.from_avatar   = rs->getString("avatar");
        fr.created_at    = rs->getString("created_at");
        out.push_back(fr);
    }
    return true;
}

bool FriendDao::IsFriend(sql::Connection* conn,
                         uint32_t user_id, uint32_t friend_id)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT COUNT(*) FROM friends "
            "WHERE user_id = ? AND friend_id = ? AND status = 1"));
    stmt->setUInt(1, user_id);
    stmt->setUInt(2, friend_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        return rs->getInt(1) > 0;
    }
    return false;
}

bool FriendDao::HasPendingRequest(sql::Connection* conn,
                                  uint32_t user_id, uint32_t other_id)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT COUNT(*) FROM friends "
            "WHERE ((user_id = ? AND friend_id = ?) OR (user_id = ? AND friend_id = ?)) "
            "AND status = 0"));
    stmt->setUInt(1, user_id);
    stmt->setUInt(2, other_id);
    stmt->setUInt(3, other_id);
    stmt->setUInt(4, user_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        return rs->getInt(1) > 0;
    }
    return false;
}

// ============================================================
// 好友备注
// ============================================================

bool FriendDao::SetRemark(sql::Connection* conn,
                          uint32_t user_id, uint32_t friend_id,
                          const std::string& remark)
{
    // 更新双向好友记录中属于自己的那条的备注
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "UPDATE friends SET remark = ? "
            "WHERE user_id = ? AND friend_id = ? AND status = 1"));

    stmt->setString(1, remark);
    stmt->setUInt(2, user_id);
    stmt->setUInt(3, friend_id);
    return stmt->executeUpdate() > 0;
}

bool FriendDao::GetFriendDetail(sql::Connection* conn,
                                uint32_t user_id, uint32_t friend_id,
                                FriendInfo& out)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT u.id, u.email, u.username, IFNULL(u.avatar, '') AS avatar, "
            "IFNULL(f.remark, '') AS remark, f.created_at "
            "FROM friends f "
            "INNER JOIN users u ON f.friend_id = u.id "
            "WHERE f.user_id = ? AND f.friend_id = ? AND f.status = 1"));

    stmt->setUInt(1, user_id);
    stmt->setUInt(2, friend_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        out.friend_id  = rs->getUInt("id");
        out.email      = rs->getString("email");
        out.username   = rs->getString("username");
        out.avatar     = rs->getString("avatar");
        out.created_at = rs->getString("created_at");
        return true;
    }
    return false;
}

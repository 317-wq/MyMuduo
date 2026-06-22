#include "db/PrivateMessageDao.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <memory>

// ============================================================
// 发送私聊消息
// ============================================================

bool PrivateMessageDao::SendMessage(sql::Connection* conn,
                                    uint32_t from_id, uint32_t to_id,
                                    const std::string& content,
                                    uint32_t& out_msg_id)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "INSERT INTO private_messages (from_user_id, to_user_id, content) "
            "VALUES (?, ?, ?)"));

    stmt->setUInt(1, from_id);
    stmt->setUInt(2, to_id);
    stmt->setString(3, content);

    try {
        stmt->executeUpdate();
    } catch (const sql::SQLException&) {
        return false;
    }

    // 获取自增 ID
    std::unique_ptr<sql::Statement> id_stmt(conn->createStatement());
    std::unique_ptr<sql::ResultSet> rs(id_stmt->executeQuery("SELECT LAST_INSERT_ID()"));
    if (rs->next()) {
        out_msg_id = rs->getUInt(1);
    }
    return true;
}

// ============================================================
// 获取对话历史
// ============================================================

bool PrivateMessageDao::GetConversation(sql::Connection* conn,
                                        uint32_t user_id, uint32_t friend_id,
                                        uint32_t after_id, int limit,
                                        std::vector<PrivateMessageRecord>& out)
{
    std::string sql =
        "SELECT id, from_user_id, to_user_id, content, is_read, created_at "
        "FROM private_messages "
        "WHERE ((from_user_id = ? AND to_user_id = ?) "
        "   OR  (from_user_id = ? AND to_user_id = ?))";

    if (after_id > 0) {
        sql += " AND id > ?";
    }
    sql += " ORDER BY id ASC LIMIT ?";

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));

    stmt->setUInt(1, user_id);
    stmt->setUInt(2, friend_id);
    stmt->setUInt(3, friend_id);
    stmt->setUInt(4, user_id);

    int param_idx = 5;
    if (after_id > 0) {
        stmt->setUInt(param_idx++, after_id);
    }
    stmt->setInt(param_idx, limit);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
        PrivateMessageRecord msg;
        msg.id           = rs->getUInt("id");
        msg.from_user_id = rs->getUInt("from_user_id");
        msg.to_user_id   = rs->getUInt("to_user_id");
        msg.content      = rs->getString("content");
        msg.is_read      = rs->getBoolean("is_read");
        msg.created_at   = rs->getString("created_at");
        out.push_back(msg);
    }
    return true;
}

// ============================================================
// 标记已读
// ============================================================

bool PrivateMessageDao::MarkAsRead(sql::Connection* conn,
                                   uint32_t user_id, uint32_t friend_id)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "UPDATE private_messages SET is_read = 1 "
            "WHERE from_user_id = ? AND to_user_id = ? AND is_read = 0"));

    stmt->setUInt(1, friend_id);
    stmt->setUInt(2, user_id);

    stmt->executeUpdate();
    return true;
}

// ============================================================
// 总未读消息数
// ============================================================

int PrivateMessageDao::GetTotalUnreadCount(sql::Connection* conn, uint32_t user_id)
{
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT COUNT(*) FROM private_messages "
            "WHERE to_user_id = ? AND is_read = 0"));

    stmt->setUInt(1, user_id);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
        return rs->getInt(1);
    }
    return 0;
}

// ============================================================
// 每个好友的未读消息数
// ============================================================

std::map<uint32_t, int> PrivateMessageDao::GetUnreadCounts(sql::Connection* conn,
                                                            uint32_t user_id)
{
    std::map<uint32_t, int> result;

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT from_user_id, COUNT(*) AS cnt "
            "FROM private_messages "
            "WHERE to_user_id = ? AND is_read = 0 "
            "GROUP BY from_user_id"));

    stmt->setUInt(1, user_id);

    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
        uint32_t friend_id = rs->getUInt("from_user_id");
        int count = rs->getInt("cnt");
        result[friend_id] = count;
    }
    return result;
}

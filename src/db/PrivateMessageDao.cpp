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
                                    uint32_t& out_msg_id,
                                    uint32_t reply_to_id)
{
    // 如果是回复消息，查询被回复消息的摘要
    std::string reply_preview;
    if (reply_to_id > 0) {
        std::unique_ptr<sql::PreparedStatement> qstmt(
            conn->prepareStatement(
                "SELECT CONCAT(u.username, ': ', LEFT(m.content, 80)) AS preview "
                "FROM private_messages m "
                "JOIN users u ON u.id = m.from_user_id "
                "WHERE m.id = ?"));
        qstmt->setUInt(1, reply_to_id);
        std::unique_ptr<sql::ResultSet> rs(qstmt->executeQuery());
        if (rs->next()) {
            reply_preview = rs->getString("preview");
        }
    }

    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "INSERT INTO private_messages (from_user_id, to_user_id, content, "
            "reply_to_id, reply_preview) "
            "VALUES (?, ?, ?, ?, ?)"));

    stmt->setUInt(1, from_id);
    stmt->setUInt(2, to_id);
    stmt->setString(3, content);
    stmt->setUInt(4, reply_to_id);       // 0 表示不是回复
    stmt->setString(5, reply_preview);   // 空串表示无引用

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
                                        std::vector<PrivateMessageRecord>& out,
                                        std::vector<PrivateMessageRecord>& updates,
                                        const std::string& updated_since)
{
    std::string base_fields =
        "id, from_user_id, to_user_id, content, is_read, is_revoked, "
        "IFNULL(reply_to_id, 0) AS reply_to_id, "
        "IFNULL(reply_preview, '') AS reply_preview, "
        "created_at, updated_at";

    // ── 新消息 ──
    {
        std::string sql =
            "SELECT " + base_fields + " "
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
            msg.id            = rs->getUInt("id");
            msg.from_user_id  = rs->getUInt("from_user_id");
            msg.to_user_id    = rs->getUInt("to_user_id");
            msg.content       = rs->getString("content");
            msg.is_read       = rs->getBoolean("is_read");
            msg.is_revoked    = rs->getBoolean("is_revoked");
            msg.reply_to_id   = rs->getUInt("reply_to_id");
            msg.reply_preview = rs->getString("reply_preview");
            msg.created_at    = rs->getString("created_at");
            msg.updated_at    = rs->getString("updated_at");
            out.push_back(msg);
        }
    }

    // ── 更新消息（撤回等） ──
    if (!updated_since.empty() && after_id > 0) {
        std::string sql =
            "SELECT " + base_fields + " "
            "FROM private_messages "
            "WHERE ((from_user_id = ? AND to_user_id = ?) "
            "   OR  (from_user_id = ? AND to_user_id = ?)) "
            "AND id <= ? AND updated_at > ? "
            "ORDER BY id ASC LIMIT 50";

        std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));

        stmt->setUInt(1, user_id);
        stmt->setUInt(2, friend_id);
        stmt->setUInt(3, friend_id);
        stmt->setUInt(4, user_id);
        stmt->setUInt(5, after_id);
        stmt->setString(6, updated_since);

        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            PrivateMessageRecord msg;
            msg.id            = rs->getUInt("id");
            msg.from_user_id  = rs->getUInt("from_user_id");
            msg.to_user_id    = rs->getUInt("to_user_id");
            msg.content       = rs->getString("content");
            msg.is_read       = rs->getBoolean("is_read");
            msg.is_revoked    = rs->getBoolean("is_revoked");
            msg.reply_to_id   = rs->getUInt("reply_to_id");
            msg.reply_preview = rs->getString("reply_preview");
            msg.created_at    = rs->getString("created_at");
            msg.updated_at    = rs->getString("updated_at");
            updates.push_back(msg);
        }
    }

    return true;
}

// ============================================================
// 撤回消息
// ============================================================

bool PrivateMessageDao::RevokeMessage(sql::Connection* conn,
                                      uint32_t msg_id, uint32_t user_id)
{
    // 查询消息的发送者和创建时间
    std::unique_ptr<sql::PreparedStatement> qstmt(
        conn->prepareStatement(
            "SELECT from_user_id, created_at "
            "FROM private_messages WHERE id = ? AND is_revoked = 0"));
    qstmt->setUInt(1, msg_id);
    std::unique_ptr<sql::ResultSet> rs(qstmt->executeQuery());

    if (!rs->next()) return false;

    uint32_t from_id = rs->getUInt("from_user_id");
    std::string created_at = rs->getString("created_at");

    // 只能撤回自己发的消息
    if (from_id != user_id) return false;

    // 检查是否在2分钟内
    std::unique_ptr<sql::PreparedStatement> tstmt(
        conn->prepareStatement(
            "SELECT TIMESTAMPDIFF(SECOND, ?, NOW()) AS diff"));
    tstmt->setString(1, created_at);
    std::unique_ptr<sql::ResultSet> trs(tstmt->executeQuery());
    if (trs->next()) {
        int diff = trs->getInt("diff");
        if (diff > 120) return false;  // 超过2分钟
    }

    // 执行撤回
    std::unique_ptr<sql::PreparedStatement> ustmt(
        conn->prepareStatement(
            "UPDATE private_messages SET is_revoked = 1 WHERE id = ?"));
    ustmt->setUInt(1, msg_id);
    ustmt->executeUpdate();
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

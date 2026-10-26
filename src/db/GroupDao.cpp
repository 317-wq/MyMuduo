#include "db/GroupDao.h"

#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include <memory>

// 创建群：chat_groups + group_members 必须在同一个事务里成功或失败。
bool GroupDao::CreateGroup(sql::Connection* conn,
                           uint32_t owner_id,
                           const std::string& name,
                           uint32_t& out_group_id)
{
    out_group_id = 0;
    bool ok = false;
    bool auto_commit_changed = false;

    try {
        conn->setAutoCommit(false);
        auto_commit_changed = true;

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "INSERT INTO chat_groups (name, owner_id) VALUES (?, ?)"));
            stmt->setString(1, name);
            stmt->setUInt(2, owner_id);
            stmt->executeUpdate();
        }

        {
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            std::unique_ptr<sql::ResultSet> rs(
                stmt->executeQuery("SELECT LAST_INSERT_ID()"));
            if (!rs->next()) {
                throw sql::SQLException("LAST_INSERT_ID failed");
            }
            out_group_id = rs->getUInt(1);
        }

        {
            std::unique_ptr<sql::PreparedStatement> stmt(
                conn->prepareStatement(
                    "INSERT INTO group_members (group_id, user_id, role) "
                    "VALUES (?, ?, 1)"));
            stmt->setUInt(1, out_group_id);
            stmt->setUInt(2, owner_id);
            stmt->executeUpdate();
        }

        conn->commit();
        ok = true;
    } catch (const std::exception&) {
        try {
            conn->rollback();
        } catch (...) {
        }
        out_group_id = 0;
    }

    if (auto_commit_changed) {
        try {
            conn->setAutoCommit(true);
        } catch (...) {
        }
    }
    return ok;
}

// 加群：先查群，再查成员，最后插入普通成员。
GroupJoinStatus GroupDao::JoinGroup(sql::Connection* conn,
                                    uint32_t group_id,
                                    uint32_t user_id,
                                    GroupInfo& out)
{
    std::string group_name;
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement("SELECT name FROM chat_groups WHERE id = ?"));
        stmt->setUInt(1, group_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            return GroupJoinStatus::kGroupNotFound;
        }
        group_name = rs->getString("name");
    }

    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT role FROM group_members WHERE group_id = ? AND user_id = ?"));
        stmt->setUInt(1, group_id);
        stmt->setUInt(2, user_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
            return GroupJoinStatus::kAlreadyMember;
        }
    }

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "INSERT INTO group_members (group_id, user_id, role) VALUES (?, ?, 0)"));
        stmt->setUInt(1, group_id);
        stmt->setUInt(2, user_id);
        stmt->executeUpdate();
    } catch (const std::exception&) {
        return GroupJoinStatus::kError;
    }

    out.id = group_id;
    out.name = group_name;
    out.my_role = 0;

    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT owner_id, created_at, "
                "(SELECT COUNT(*) FROM group_members WHERE group_id = ?) AS cnt "
                "FROM chat_groups WHERE id = ?"));
        stmt->setUInt(1, group_id);
        stmt->setUInt(2, group_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
            out.owner_id = rs->getUInt("owner_id");
            out.created_at = rs->getString("created_at");
            out.member_count = rs->getInt("cnt");
        }
    }

    return GroupJoinStatus::kOk;
}

// 退群：群主不能直接退，普通成员删除成员记录。
GroupLeaveStatus GroupDao::LeaveGroup(sql::Connection* conn,
                                      uint32_t group_id,
                                      uint32_t user_id)
{
    uint32_t owner_id = 0;
    int role = -1;
    {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "SELECT g.owner_id, gm.role "
                "FROM chat_groups g "
                "LEFT JOIN group_members gm "
                "  ON gm.group_id = g.id AND gm.user_id = ? "
                "WHERE g.id = ?"));
        stmt->setUInt(1, user_id);
        stmt->setUInt(2, group_id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            return GroupLeaveStatus::kGroupNotFound;
        }
        owner_id = rs->getUInt("owner_id");
        role = rs->isNull("role") ? -1 : rs->getInt("role");
    }

    if (role < 0) {
        return GroupLeaveStatus::kNotMember;
    }
    if (owner_id == user_id || role == 1) {
        return GroupLeaveStatus::kOwnerCannotLeave;
    }

    try {
        std::unique_ptr<sql::PreparedStatement> stmt(
            conn->prepareStatement(
                "DELETE FROM group_members WHERE group_id = ? AND user_id = ?"));
        stmt->setUInt(1, group_id);
        stmt->setUInt(2, user_id);
        stmt->executeUpdate();
    } catch (const std::exception&) {
        return GroupLeaveStatus::kError;
    }

    return GroupLeaveStatus::kOk;
}

// 群列表：按用户查成员表，带成员数和角色。
bool GroupDao::ListGroups(sql::Connection* conn,
                          uint32_t user_id,
                          std::vector<GroupInfo>& out)
{
    out.clear();
    std::unique_ptr<sql::PreparedStatement> stmt(
        conn->prepareStatement(
            "SELECT g.id, g.name, g.owner_id, g.created_at, gm.role, "
            "  (SELECT COUNT(*) FROM group_members m2 WHERE m2.group_id = g.id) AS member_count "
            "FROM group_members gm "
            "JOIN chat_groups g ON g.id = gm.group_id "
            "WHERE gm.user_id = ? "
            "ORDER BY g.id DESC"));
    stmt->setUInt(1, user_id);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    while (rs->next()) {
        GroupInfo info;
        info.id = rs->getUInt("id");
        info.name = rs->getString("name");
        info.owner_id = rs->getUInt("owner_id");
        info.created_at = rs->getString("created_at");
        info.my_role = rs->getInt("role");
        info.member_count = rs->getInt("member_count");
        out.push_back(std::move(info));
    }
    return true;
}

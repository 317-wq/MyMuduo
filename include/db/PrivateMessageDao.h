#pragma once

/*
    私聊消息数据访问对象 (Data Access Object)

    所有方法都是同步的，接收 sql::Connection*，
    由调用者通过 Database::Execute 在 DB 线程执行。

    使用 Prepared Statement 防 SQL 注入。
*/

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <cstdint>
#include <string>
#include <vector>
#include <map>

// 私聊消息记录
struct PrivateMessageRecord {
    uint32_t id = 0;
    uint32_t from_user_id = 0;
    uint32_t to_user_id = 0;
    std::string content;
    bool is_read = false;
    bool is_revoked = false;
    uint32_t reply_to_id = 0;
    std::string reply_preview;
    std::string created_at;
    std::string updated_at;
};

class PrivateMessageDao {
public:
    // 发送私聊消息
    // reply_to_id: 回复的消息 ID，0 表示不是回复
    static bool SendMessage(sql::Connection* conn,
                           uint32_t from_id, uint32_t to_id,
                           const std::string& content,
                           uint32_t& out_msg_id,
                           uint32_t reply_to_id = 0);

    // 获取两个用户之间的对话历史
    // after_id: 只返回 id > after_id 的消息（用于增量轮询），0 表示从头开始
    // limit: 最多返回条数
    // updated_since: 非空时同时返回 id <= after_id 但 updated_at > updated_since 的更新消息
    static bool GetConversation(sql::Connection* conn,
                               uint32_t user_id, uint32_t friend_id,
                               uint32_t after_id, int limit,
                               std::vector<PrivateMessageRecord>& out,
                               std::vector<PrivateMessageRecord>& updates,
                               const std::string& updated_since = "");

    // 撤回消息（2分钟内，仅发送者）
    // 返回 true 表示撤回成功
    static bool RevokeMessage(sql::Connection* conn,
                             uint32_t msg_id, uint32_t user_id);

    // 标记某好友发来的所有消息为已读
    static bool MarkAsRead(sql::Connection* conn,
                          uint32_t user_id, uint32_t friend_id);

    // 获取某个用户的总未读消息数
    static int GetTotalUnreadCount(sql::Connection* conn, uint32_t user_id);

    // 获取每个好友的未读消息数
    // 返回 map: friend_id → unread_count
    static std::map<uint32_t, int> GetUnreadCounts(sql::Connection* conn,
                                                    uint32_t user_id);
};

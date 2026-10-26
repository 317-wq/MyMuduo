#pragma once

/*
    群聊数据访问对象

    负责 chat_groups / group_members 两张表的同步 SQL；
    异步执行和业务错误码由 GroupService 负责。
*/

#include <cppconn/connection.h>

#include <cstdint>
#include <string>
#include <vector>

// 群消息记录（历史/推送使用）
struct GroupMessageRecord {
    uint32_t id = 0;
    uint32_t group_id = 0;
    uint32_t from_user_id = 0;
    std::string from_username;
    std::string content;
    bool is_revoked = false;
    std::string created_at;
    std::string updated_at;
};

// 群信息（列表/加群回包使用）
struct GroupInfo {
    uint32_t id = 0;
    std::string name;
    uint32_t owner_id = 0;
    int member_count = 0;
    int my_role = 0;          // 0=member, 1=owner
    std::string created_at;
};

enum class GroupJoinStatus {
    kOk,
    kGroupNotFound,
    kAlreadyMember,
    kError
};

enum class GroupLeaveStatus {
    kOk,
    kGroupNotFound,
    kNotMember,
    kOwnerCannotLeave,
    kError
};

class GroupDao {
public:
    // 创建群，并在同一个事务里把 owner 加入成员表。
    static bool CreateGroup(sql::Connection* conn,
                            uint32_t owner_id,
                            const std::string& name,
                            uint32_t& out_group_id);

    // 加群：群必须存在，且不能重复加入。
    static GroupJoinStatus JoinGroup(sql::Connection* conn,
                                     uint32_t group_id,
                                     uint32_t user_id,
                                     GroupInfo& out);

    // 退群：群主不能直接退群（MVP 不做转让/解散）。
    static GroupLeaveStatus LeaveGroup(sql::Connection* conn,
                                      uint32_t group_id,
                                      uint32_t user_id);

    // 查询用户加入的群（带成员数和我的角色）。
    static bool ListGroups(sql::Connection* conn,
                           uint32_t user_id,
                           std::vector<GroupInfo>& out);

    // 是否群成员。
    static bool IsMember(sql::Connection* conn,
                         uint32_t group_id, uint32_t user_id);

    // 群消息落库，返回自增 msg_id。
    static bool SendGroupMessage(sql::Connection* conn,
                                 uint32_t group_id, uint32_t from_id,
                                 const std::string& content,
                                 uint32_t& out_msg_id);

    // 群历史：id > after_id，多查一条判断 has_more。
    static bool GetGroupHistory(sql::Connection* conn,
                                uint32_t group_id, uint32_t after_id,
                                int limit,
                                std::vector<GroupMessageRecord>& out,
                                bool& has_more);

    // 在线 fanout 用的成员 id 列表。
    static bool ListMemberIds(sql::Connection* conn,
                              uint32_t group_id,
                              std::vector<uint32_t>& out);
};

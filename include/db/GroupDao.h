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
};

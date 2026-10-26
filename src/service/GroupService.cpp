#include "service/GroupService.h"

#include <memory>

// 创建群：名称 1..64 字节，DAO 负责事务。
void GroupService::CreateGroup(uint32_t owner_id, const std::string& name,
                               CreateCallback callback)
{
    if (owner_id == 0 || name.empty() || name.size() > 64) {
        callback(1, "群名称不能为空且不超过 64 字节", 0);
        return;
    }

    auto group_id = std::make_shared<uint32_t>(0);
    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("创建失败");

    _db->Execute(_loop,
        [owner_id, name, group_id, error, msg](sql::Connection* conn) {
            if (GroupDao::CreateGroup(conn, owner_id, name, *group_id)) {
                *error = 0;
                *msg = "创建成功";
            }
        },
        [callback, group_id, error, msg]() {
            callback(*error, *msg, *group_id);
        });
}

// 加群：DAO 返回状态，业务层映射为错误码。
void GroupService::JoinGroup(uint32_t user_id, uint32_t group_id,
                             JoinCallback callback)
{
    if (user_id == 0 || group_id == 0) {
        callback(1, "参数错误", GroupInfo{});
        return;
    }

    auto info = std::make_shared<GroupInfo>();
    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("加入失败");

    _db->Execute(_loop,
        [user_id, group_id, info, error, msg](sql::Connection* conn) {
            switch (GroupDao::JoinGroup(conn, group_id, user_id, *info)) {
                case GroupJoinStatus::kOk:
                    *error = 0;
                    *msg = "加入成功";
                    break;
                case GroupJoinStatus::kGroupNotFound:
                    *error = 2;
                    *msg = "群不存在";
                    break;
                case GroupJoinStatus::kAlreadyMember:
                    *error = 3;
                    *msg = "已经在群里";
                    break;
                case GroupJoinStatus::kError:
                default:
                    *error = 5;
                    *msg = "加入失败";
                    break;
            }
        },
        [callback, info, error, msg]() {
            callback(*error, *msg, *info);
        });
}

// 退群：群主不能直接退群（MVP）。
void GroupService::LeaveGroup(uint32_t user_id, uint32_t group_id,
                              LeaveCallback callback)
{
    if (user_id == 0 || group_id == 0) {
        callback(1, "参数错误");
        return;
    }

    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("退群失败");

    _db->Execute(_loop,
        [user_id, group_id, error, msg](sql::Connection* conn) {
            switch (GroupDao::LeaveGroup(conn, group_id, user_id)) {
                case GroupLeaveStatus::kOk:
                    *error = 0;
                    *msg = "已退群";
                    break;
                case GroupLeaveStatus::kGroupNotFound:
                    *error = 2;
                    *msg = "群不存在";
                    break;
                case GroupLeaveStatus::kNotMember:
                    *error = 4;
                    *msg = "不在群里";
                    break;
                case GroupLeaveStatus::kOwnerCannotLeave:
                    *error = 3;
                    *msg = "群主不能退群";
                    break;
                case GroupLeaveStatus::kError:
                default:
                    *error = 5;
                    *msg = "退群失败";
                    break;
            }
        },
        [callback, error, msg]() {
            callback(*error, *msg);
        });
}

// 群列表。
void GroupService::ListGroups(uint32_t user_id, ListCallback callback)
{
    if (user_id == 0) {
        callback(1, "参数错误", {});
        return;
    }

    auto groups = std::make_shared<std::vector<GroupInfo>>();
    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("查询失败");

    _db->Execute(_loop,
        [user_id, groups, error, msg](sql::Connection* conn) {
            if (GroupDao::ListGroups(conn, user_id, *groups)) {
                *error = 0;
                *msg = "ok";
            }
        },
        [callback, groups, error, msg]() {
            callback(*error, *msg, *groups);
        });
}

#include "service/GroupService.h"
#include "db/UserDao.h"

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

// 群消息：成员校验 -> 落库 -> 取成员列表（在线 fanout 用）。
void GroupService::SendGroupMessage(uint32_t user_id, uint32_t group_id,
                                    const std::string& content,
                                    SendMessageCallback callback)
{
    if (user_id == 0 || group_id == 0 || content.empty() || content.size() > 65535) {
        callback(1, "参数错误", GroupMessageRecord{}, {});
        return;
    }

    auto record = std::make_shared<GroupMessageRecord>();
    auto members = std::make_shared<std::vector<uint32_t>>();
    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("发送失败");

    _db->Execute(_loop,
        [user_id, group_id, content, record, members, error, msg](sql::Connection* conn) {
            if (!GroupDao::IsMember(conn, group_id, user_id)) {
                *error = 4;
                *msg = "不在群里";
                return;
            }

            UserInfo sender;
            if (!UserDao::GetUserById(conn, user_id, sender)) {
                *error = 5;
                *msg = "发送者不存在";
                return;
            }

            if (!GroupDao::SendGroupMessage(conn, group_id, user_id, content, record->id)) {
                *error = 5;
                *msg = "消息保存失败";
                return;
            }

            record->group_id = group_id;
            record->from_user_id = user_id;
            record->from_username = sender.username;
            record->content = content;

            GroupDao::ListMemberIds(conn, group_id, *members);
            *error = 0;
            *msg = "发送成功";
        },
        [callback, record, members, error, msg]() {
            callback(*error, *msg, *record, *members);
        });
}

// 群历史：成员校验后按 id 增量查询。
void GroupService::GetGroupHistory(uint32_t user_id, uint32_t group_id,
                                   uint32_t after_id, int limit,
                                   HistoryCallback callback)
{
    if (user_id == 0 || group_id == 0) {
        callback(1, "参数错误", {}, false, after_id);
        return;
    }
    if (limit <= 0) limit = 50;
    if (limit > 200) limit = 200;

    auto records = std::make_shared<std::vector<GroupMessageRecord>>();
    auto has_more = std::make_shared<bool>(false);
    auto last_msg_id = std::make_shared<uint32_t>(after_id);
    auto error = std::make_shared<int>(5);
    auto msg = std::make_shared<std::string>("查询失败");

    _db->Execute(_loop,
        [user_id, group_id, after_id, limit, records, has_more, last_msg_id, error, msg]
        (sql::Connection* conn) {
            if (!GroupDao::IsMember(conn, group_id, user_id)) {
                *error = 4;
                *msg = "不在群里";
                return;
            }
            if (!GroupDao::GetGroupHistory(conn, group_id, after_id, limit,
                                           *records, *has_more)) {
                return;
            }
            for (const auto& r : *records) {
                if (r.id > *last_msg_id) *last_msg_id = r.id;
            }
            *error = 0;
            *msg = "ok";
        },
        [callback, records, has_more, last_msg_id, error, msg]() {
            callback(*error, *msg, *records, *has_more, *last_msg_id);
        });
}

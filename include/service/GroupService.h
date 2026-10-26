#pragma once

/*
    群聊业务层

    负责参数校验、DAO 结果到业务错误码的映射、异步回调；
    ChatServer 只做协议解析和回包，不直接写群 SQL。
*/

#include "base/NoCopy.h"
#include "db/Database.h"
#include "db/GroupDao.h"
#include "net/EventLoop.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GroupService : NoCopy {
public:
    // error_code: 0=成功，1=参数错误，2=群不存在，3=已在群/群主不能退，
    //             4=不在群，5=数据库错误
    using CreateCallback = std::function<void(int, const std::string&, uint32_t)>;
    using JoinCallback = std::function<void(int, const std::string&, const GroupInfo&)>;
    using LeaveCallback = std::function<void(int, const std::string&)>;
    using ListCallback = std::function<void(int, const std::string&,
                                            const std::vector<GroupInfo>&)>;
    using SendMessageCallback = std::function<void(int, const std::string&,
                                                   const GroupMessageRecord&,
                                                   const std::vector<uint32_t>&)>;
    using HistoryCallback = std::function<void(int, const std::string&,
                                               const std::vector<GroupMessageRecord>&,
                                               bool has_more, uint32_t last_msg_id)>;

    GroupService(Database* db, EventLoop* loop)
        : _db(db), _loop(loop) {}

    void CreateGroup(uint32_t owner_id, const std::string& name,
                     CreateCallback callback);
    void JoinGroup(uint32_t user_id, uint32_t group_id,
                   JoinCallback callback);
    void LeaveGroup(uint32_t user_id, uint32_t group_id,
                    LeaveCallback callback);
    void ListGroups(uint32_t user_id, ListCallback callback);

    // 先校验成员，再落库；回调带成员 id 列表供 ChatServer 在线 fanout。
    void SendGroupMessage(uint32_t user_id, uint32_t group_id,
                          const std::string& content,
                          SendMessageCallback callback);

    // 群历史：只允许群成员查询。
    void GetGroupHistory(uint32_t user_id, uint32_t group_id,
                         uint32_t after_id, int limit,
                         HistoryCallback callback);

private:
    Database* _db;
    EventLoop* _loop;
};

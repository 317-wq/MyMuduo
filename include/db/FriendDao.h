#pragma once

/*
    好友数据访问对象 (Data Access Object)

    所有方法都是同步的，接收 sql::Connection*，
    由调用者通过 Database::Execute 在 DB 线程执行。

    使用 Prepared Statement 防 SQL 注入。

    好友关系模型（双向）：
    - A 发请求 → INSERT (user_id=A, friend_id=B, status=0)
    - B 同意   → UPDATE 原记录 status=1 + INSERT (user_id=B, friend_id=A, status=1)
    - 任一方删除 → DELETE 两条记录
*/

#include "db/UserDao.h"

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>

#include <cstdint>
#include <string>
#include <vector>

// 好友信息
struct FriendInfo {
    uint32_t friend_id = 0;
    std::string email;
    std::string username;
    std::string avatar;
    std::string remark;       // 好友备注名
    bool online = false;
    std::string created_at;  // 成为好友的时间
};

// 好友请求（别人发给我的待处理请求）
struct FriendRequest {
    uint32_t id = 0;             // friends 表的主键
    uint32_t from_user_id = 0;
    std::string from_email;
    std::string from_username;
    std::string from_avatar;
    std::string created_at;
};

class FriendDao {
public:
    // ============================================================
    // 用户搜索
    // ============================================================

    // 按邮箱搜索用户（LIKE 模糊匹配），排除指定用户
    static bool SearchUserByEmail(sql::Connection* conn,
                                  const std::string& keyword,
                                  uint32_t exclude_user_id,
                                  std::vector<UserInfo>& out);

    // ============================================================
    // 好友请求
    // ============================================================

    // 发送好友请求
    // 如果对方已经发来请求（反向记录存在），自动接受 → 双方成为好友
    // out_request_id: 新创建的请求 ID（或已有请求的 ID）
    // out_auto_accepted: 是否因反向请求已存在而自动成为好友
    static bool SendFriendRequest(sql::Connection* conn,
                                  uint32_t from_id, uint32_t to_id,
                                  uint32_t& out_request_id,
                                  bool& out_auto_accepted);

    // 同意好友请求 → 双向好友
    // request_id 是 friends 表的主键
    static bool AcceptFriendRequest(sql::Connection* conn,
                                    uint32_t user_id, uint32_t request_id,
                                    uint32_t& out_friend_id);

    // 拒绝好友请求 → 删除记录
    static bool RejectFriendRequest(sql::Connection* conn,
                                    uint32_t user_id, uint32_t request_id);

    // ============================================================
    // 好友管理
    // ============================================================

    // 删除好友（双向删除所有记录）
    // 返回删除的条数
    static bool DeleteFriend(sql::Connection* conn,
                             uint32_t user_id, uint32_t friend_id);

    // 获取好友列表（仅已接受的）
    static bool GetFriendList(sql::Connection* conn,
                              uint32_t user_id,
                              std::vector<FriendInfo>& out);

    // 获取待处理的好友请求（别人发给我的，status=0 且 friend_id=me）
    static bool GetPendingRequests(sql::Connection* conn,
                                   uint32_t user_id,
                                   std::vector<FriendRequest>& out);

    // 检查是否已是好友（双向已接受）
    static bool IsFriend(sql::Connection* conn,
                         uint32_t user_id, uint32_t friend_id);

    // 检查是否有待处理的好友请求（任意方向）
    static bool HasPendingRequest(sql::Connection* conn,
                                  uint32_t user_id, uint32_t other_id);

    // 设置好友备注
    static bool SetRemark(sql::Connection* conn,
                         uint32_t user_id, uint32_t friend_id,
                         const std::string& remark);

    // 获取好友详细信息（含备注）
    static bool GetFriendDetail(sql::Connection* conn,
                               uint32_t user_id, uint32_t friend_id,
                               FriendInfo& out);
};

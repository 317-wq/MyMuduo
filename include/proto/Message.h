#pragma once

/*
    消息基类 + 具体消息类型

    所有消息继承自 Message，实现：
    - GetType()   : 返回消息类型枚举
    - ToJson()    : 序列化为 jsoncpp::Value
    - FromJson()  : 从 jsoncpp::Value 反序列化

    工厂方法 Message::Create(type) 根据类型创建对应的空消息对象。
*/

#include "proto/MessageType.h"

#include <jsoncpp/json/json.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

// 抽象类
class Message {
public:
    using Ptr = std::shared_ptr<Message>;

    virtual ~Message() = default;

    // 返回消息类型
    virtual MessageType GetType() const = 0;

    // 序列化为 JSON
    virtual Json::Value ToJson() const = 0;

    // 从 JSON 反序列化，返回 false 表示解析失败
    virtual bool FromJson(const Json::Value& root) = 0;

    // 序列化为 JSON 字符串
    std::string ToJsonString() const;

    // 从 JSON 字符串反序列化
    bool FromJsonString(const std::string& json_str);

    // 工厂方法：根据消息类型创建对应的空 Message 对象
    static Ptr Create(MessageType type);
};

// ============================================================
// 具体消息实现
// ============================================================

// ---------- 心跳 ----------
class HeartbeatMessage : public Message {
public:
    MessageType GetType() const override { return MessageType::kHeartbeat; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
};

// ---------- 登录请求 ----------
class LoginRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kLoginRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    std::string email;       // 邮箱登录
    std::string password;
};

// ---------- 登录响应 ----------
class LoginResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kLoginResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    uint32_t user_id = 0;
    std::string username;
    std::string avatar;
    std::string message;
};

// ---------- 注册请求（含验证码） ----------
class RegisterRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kRegisterRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    std::string email;
    std::string code;        // 验证码
    std::string password;
    std::string username;
};

// ---------- 注册响应 ----------
class RegisterResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kRegisterResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    uint32_t user_id = 0;
    std::string message;
};

// ---------- 发送验证码请求 ----------
class SendCodeRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kSendCodeRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    std::string email;
    int type = 1;            // 1=注册, 2=重置密码
};

// ---------- 发送验证码响应 ----------
class SendCodeResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kSendCodeResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
};

// ---------- 聊天消息（广播） ----------
class ChatMessage : public Message {
public:
    MessageType GetType() const override { return MessageType::kChatMessage; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t user_id = 0;
    std::string username;
    std::string content;
    int64_t timestamp = 0;
};

// ---------- 私聊消息 ----------
class PrivateMessage : public Message {
public:
    MessageType GetType() const override { return MessageType::kPrivateMessage; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t msg_id = 0;         // 服务端生成的全局消息 ID
    uint32_t client_msg_id = 0;  // 客户端生成的请求 ID，用于 ack 匹配
    uint32_t from_user_id = 0;
    std::string from_username;
    uint32_t to_user_id = 0;
    std::string content;
    int64_t timestamp = 0;
    std::string created_at;   // 服务端入库时间，同步/推送展示用
};

// ---------- 私聊发送回执 ----------
class PrivateMessageAck : public Message {
public:
    MessageType GetType() const override { return MessageType::kPrivateMessageAck; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t client_msg_id = 0;
    uint32_t msg_id = 0;
    bool success = false;
    std::string message;
};

// ---------- 私聊历史 ----------
class PrivateHistoryRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kPrivateHistoryRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t friend_id = 0;
    uint32_t after_id = 0;   // 只拉 id > after_id 的消息；0 表示从头
    int limit = 50;
};

class PrivateHistoryResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kPrivateHistoryResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
    uint32_t friend_id = 0;

    struct Item {
        uint32_t msg_id = 0;
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
    std::vector<Item> messages;
};

// ---------- 未读数 ----------
class UnreadRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kUnreadRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
};

class UnreadResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kUnreadResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    int total = 0;
    std::map<uint32_t, int> counts;  // friend_id -> unread count
};

// ---------- 标记已读 ----------
class MarkReadRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kMarkReadRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t friend_id = 0;
};

class MarkReadResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kMarkReadResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
};

// ---------- 离线增量同步 ----------
class SyncRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kSyncRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t last_private_id = 0;
    int limit = 200;
};

class SyncResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kSyncResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
    uint32_t last_msg_id = 0;
    bool has_more = false;
    std::vector<PrivateMessage> messages;
};

// ---------- 群聊基础 ----------
class CreateGroupRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kCreateGroupRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    std::string name;
};

class CreateGroupResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kCreateGroupResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    bool success = false;
    std::string message;
    uint32_t group_id = 0;
};

class JoinGroupRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kJoinGroupRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    uint32_t group_id = 0;
};

class JoinGroupResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kJoinGroupResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    bool success = false;
    std::string message;
    uint32_t group_id = 0;
    std::string group_name;
    int member_count = 0;
    int my_role = 0;
};

class LeaveGroupRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kLeaveGroupRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    uint32_t group_id = 0;
};

class LeaveGroupResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kLeaveGroupResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    bool success = false;
    std::string message;
};

class GroupListRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kGroupListRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
};

class GroupListResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kGroupListResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
    bool success = false;
    std::string message;

    struct Item {
        uint32_t group_id = 0;
        std::string name;
        uint32_t owner_id = 0;
        int member_count = 0;
        int my_role = 0;
        std::string created_at;
    };
    std::vector<Item> groups;
};

// ---------- 系统消息（服务器推送） ----------
class SystemMessage : public Message {
public:
    MessageType GetType() const override { return MessageType::kSystemMessage; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    std::string content;
    int64_t timestamp = 0;
};

// ---------- 登出请求 ----------
class LogoutRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kLogoutRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t user_id = 0;
};

// ---------- 登出响应 ----------
class LogoutResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kLogoutResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
};

// ---------- 搜索用户请求 ----------
class SearchUserRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kSearchUserRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    std::string keyword;
};

// ---------- 搜索用户响应 ----------
class SearchUserResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kSearchUserResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    // users: [{id, email, username, avatar}, ...]
    struct UserItem {
        uint32_t id = 0;
        std::string email;
        std::string username;
        std::string avatar;
    };
    std::vector<UserItem> users;
};

// ---------- 添加好友请求 ----------
class AddFriendRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kAddFriendRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t to_user_id = 0;
};

// ---------- 添加好友响应 ----------
class AddFriendResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kAddFriendResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
};

// ---------- 同意好友请求 ----------
class AcceptFriendRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kAcceptFriendRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t request_id = 0;
};

// ---------- 同意好友响应 ----------
class AcceptFriendResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kAcceptFriendResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
    uint32_t friend_id = 0;
    std::string friend_email;
    std::string friend_username;
    std::string friend_avatar;
};

// ---------- 删除好友请求 ----------
class DeleteFriendRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kDeleteFriendRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    uint32_t friend_id = 0;
};

// ---------- 删除好友响应 ----------
class DeleteFriendResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kDeleteFriendResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    std::string message;
};

// ---------- 好友列表请求 ----------
class FriendListRequest : public Message {
public:
    MessageType GetType() const override { return MessageType::kFriendListRequest; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;
};

// ---------- 好友列表响应 ----------
class FriendListResponse : public Message {
public:
    MessageType GetType() const override { return MessageType::kFriendListResponse; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    bool success = false;
    struct FriendItem {
        uint32_t id = 0;
        std::string email;
        std::string username;
        std::string avatar;
        std::string remark;
        bool online = false;
    };
    std::vector<FriendItem> friends;
};

// ---------- 错误消息 ----------
class ErrorMessage : public Message {
public:
    MessageType GetType() const override { return MessageType::kError; }
    Json::Value ToJson() const override;
    bool FromJson(const Json::Value& root) override;

    int code = 0;
    std::string message;

    ErrorMessage() = default;
    ErrorMessage(int c, std::string m) : code(c), message(std::move(m)) {}
};

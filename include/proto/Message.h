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
#include <memory>
#include <string>
#include <cstdint>

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

    uint32_t from_user_id = 0;
    std::string from_username;
    uint32_t to_user_id = 0;
    std::string content;
    int64_t timestamp = 0;
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

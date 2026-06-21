#include "proto/Message.h"
#include "proto/MessageType.h"

#include <jsoncpp/json/json.h>
#include <memory>
#include <stdexcept>
#include <sstream>

// ============================================================
// Message 基类
// ============================================================

std::string Message::ToJsonString() const {
    Json::Value root = ToJson();
    Json::StreamWriterBuilder builder;
    builder["emitUTF8"] = true;
    builder["indentation"] = "";  // 紧凑输出
    return Json::writeString(builder, root);
}

bool Message::FromJsonString(const std::string& json_str) {
    Json::CharReaderBuilder builder;
    std::string errs;
    Json::Value root;
    std::istringstream iss(json_str);
    if (!Json::parseFromStream(builder, iss, &root, &errs)) {
        return false;
    }
    return FromJson(root);
}

// 工厂方法
Message::Ptr Message::Create(MessageType type) {
    switch (type) {
        case MessageType::kHeartbeat:
            return std::make_shared<HeartbeatMessage>();
        case MessageType::kLoginRequest:
            return std::make_shared<LoginRequest>();
        case MessageType::kLoginResponse:
            return std::make_shared<LoginResponse>();
        case MessageType::kRegisterRequest:
            return std::make_shared<RegisterRequest>();
        case MessageType::kRegisterResponse:
            return std::make_shared<RegisterResponse>();
        case MessageType::kSendCodeRequest:
            return std::make_shared<SendCodeRequest>();
        case MessageType::kSendCodeResponse:
            return std::make_shared<SendCodeResponse>();
        case MessageType::kChatMessage:
            return std::make_shared<ChatMessage>();
        case MessageType::kPrivateMessage:
            return std::make_shared<PrivateMessage>();
        case MessageType::kSystemMessage:
            return std::make_shared<SystemMessage>();
        case MessageType::kLogoutRequest:
            return std::make_shared<LogoutRequest>();
        case MessageType::kLogoutResponse:
            return std::make_shared<LogoutResponse>();
        case MessageType::kError:
            return std::make_shared<ErrorMessage>();
        default:
            return nullptr;
    }
}

// ============================================================
// HeartbeatMessage
// ============================================================

Json::Value HeartbeatMessage::ToJson() const {
    Json::Value root;
    root["type"] = "heartbeat";
    return root;
}

bool HeartbeatMessage::FromJson(const Json::Value& root) {
    (void)root;
    return true;
}

// ============================================================
// LoginRequest
// ============================================================

Json::Value LoginRequest::ToJson() const {
    Json::Value root;
    root["type"] = "login_request";
    root["email"] = email;
    root["password"] = password;
    return root;
}

bool LoginRequest::FromJson(const Json::Value& root) {
    if (!root.isMember("email") || !root.isMember("password"))
        return false;
    email = root["email"].asString();
    password = root["password"].asString();
    return true;
}

// ============================================================
// LoginResponse
// ============================================================

Json::Value LoginResponse::ToJson() const {
    Json::Value root;
    root["type"] = "login_response";
    root["success"] = success;
    root["user_id"] = static_cast<Json::UInt>(user_id);
    root["username"] = username;
    root["avatar"] = avatar;
    root["message"] = message;
    return root;
}

bool LoginResponse::FromJson(const Json::Value& root) {
    if (!root.isMember("success"))
        return false;
    success = root["success"].asBool();
    user_id = root.get("user_id", 0).asUInt();
    username = root.get("username", "").asString();
    avatar = root.get("avatar", "").asString();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// RegisterRequest
// ============================================================

Json::Value RegisterRequest::ToJson() const {
    Json::Value root;
    root["type"] = "register_request";
    root["email"] = email;
    root["code"] = code;
    root["password"] = password;
    root["username"] = username;
    return root;
}

bool RegisterRequest::FromJson(const Json::Value& root) {
    if (!root.isMember("email") || !root.isMember("password"))
        return false;
    email = root["email"].asString();
    code = root.get("code", "").asString();
    password = root["password"].asString();
    username = root.get("username", "").asString();
    return true;
}

// ============================================================
// RegisterResponse
// ============================================================

Json::Value RegisterResponse::ToJson() const {
    Json::Value root;
    root["type"] = "register_response";
    root["success"] = success;
    root["user_id"] = static_cast<Json::UInt>(user_id);
    root["message"] = message;
    return root;
}

bool RegisterResponse::FromJson(const Json::Value& root) {
    if (!root.isMember("success"))
        return false;
    success = root["success"].asBool();
    user_id = root.get("user_id", 0).asUInt();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// SendCodeRequest
// ============================================================

Json::Value SendCodeRequest::ToJson() const {
    Json::Value root;
    root["type"] = "send_code_request";
    root["email"] = email;
    root["code_type"] = type;
    return root;
}

bool SendCodeRequest::FromJson(const Json::Value& root) {
    if (!root.isMember("email"))
        return false;
    email = root["email"].asString();
    type = root.get("code_type", 1).asInt();
    return true;
}

// ============================================================
// SendCodeResponse
// ============================================================

Json::Value SendCodeResponse::ToJson() const {
    Json::Value root;
    root["type"] = "send_code_response";
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool SendCodeResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// ChatMessage
// ============================================================

Json::Value ChatMessage::ToJson() const {
    Json::Value root;
    root["type"] = "chat_message";
    root["user_id"] = static_cast<Json::UInt>(user_id);
    root["username"] = username;
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(timestamp);
    return root;
}

bool ChatMessage::FromJson(const Json::Value& root) {
    if (!root.isMember("content"))
        return false;
    user_id = root.get("user_id", 0).asUInt();
    username = root.get("username", "").asString();
    content = root["content"].asString();
    timestamp = root.get("timestamp", 0).asInt64();
    return true;
}

// ============================================================
// PrivateMessage
// ============================================================

Json::Value PrivateMessage::ToJson() const {
    Json::Value root;
    root["type"] = "private_message";
    root["from_user_id"] = static_cast<Json::UInt>(from_user_id);
    root["from_username"] = from_username;
    root["to_user_id"] = static_cast<Json::UInt>(to_user_id);
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(timestamp);
    return root;
}

bool PrivateMessage::FromJson(const Json::Value& root) {
    if (!root.isMember("content"))
        return false;
    from_user_id = root.get("from_user_id", 0).asUInt();
    from_username = root.get("from_username", "").asString();
    to_user_id = root.get("to_user_id", 0).asUInt();
    content = root["content"].asString();
    timestamp = root.get("timestamp", 0).asInt64();
    return true;
}

// ============================================================
// SystemMessage
// ============================================================

Json::Value SystemMessage::ToJson() const {
    Json::Value root;
    root["type"] = "system_message";
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(timestamp);
    return root;
}

bool SystemMessage::FromJson(const Json::Value& root) {
    if (!root.isMember("content"))
        return false;
    content = root["content"].asString();
    timestamp = root.get("timestamp", 0).asInt64();
    return true;
}

// ============================================================
// LogoutRequest
// ============================================================

Json::Value LogoutRequest::ToJson() const {
    Json::Value root;
    root["type"] = "logout_request";
    root["user_id"] = static_cast<Json::UInt>(user_id);
    return root;
}

bool LogoutRequest::FromJson(const Json::Value& root) {
    user_id = root.get("user_id", 0).asUInt();
    return true;
}

// ============================================================
// LogoutResponse
// ============================================================

Json::Value LogoutResponse::ToJson() const {
    Json::Value root;
    root["type"] = "logout_response";
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool LogoutResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// ErrorMessage
// ============================================================

Json::Value ErrorMessage::ToJson() const {
    Json::Value root;
    root["type"] = "error";
    root["code"] = code;
    root["message"] = message;
    return root;
}

bool ErrorMessage::FromJson(const Json::Value& root) {
    code = root.get("code", 0).asInt();
    message = root.get("message", "").asString();
    return true;
}

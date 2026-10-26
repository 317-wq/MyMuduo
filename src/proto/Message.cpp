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
    Json::Value root = ToJson(); // 也是运行时多态，需要注册对应的type
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
    return FromJson(root); // 运行时多态
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
        case MessageType::kPrivateMessageAck:
            return std::make_shared<PrivateMessageAck>();
        case MessageType::kPrivateHistoryRequest:
            return std::make_shared<PrivateHistoryRequest>();
        case MessageType::kPrivateHistoryResponse:
            return std::make_shared<PrivateHistoryResponse>();
        case MessageType::kUnreadRequest:
            return std::make_shared<UnreadRequest>();
        case MessageType::kUnreadResponse:
            return std::make_shared<UnreadResponse>();
        case MessageType::kMarkReadRequest:
            return std::make_shared<MarkReadRequest>();
        case MessageType::kMarkReadResponse:
            return std::make_shared<MarkReadResponse>();
        case MessageType::kSyncRequest:
            return std::make_shared<SyncRequest>();
        case MessageType::kSyncResponse:
            return std::make_shared<SyncResponse>();
        case MessageType::kSystemMessage:
            return std::make_shared<SystemMessage>();
        case MessageType::kLogoutRequest:
            return std::make_shared<LogoutRequest>();
        case MessageType::kLogoutResponse:
            return std::make_shared<LogoutResponse>();
        case MessageType::kSearchUserRequest:
            return std::make_shared<SearchUserRequest>();
        case MessageType::kSearchUserResponse:
            return std::make_shared<SearchUserResponse>();
        case MessageType::kAddFriendRequest:
            return std::make_shared<AddFriendRequest>();
        case MessageType::kAddFriendResponse:
            return std::make_shared<AddFriendResponse>();
        case MessageType::kAcceptFriendRequest:
            return std::make_shared<AcceptFriendRequest>();
        case MessageType::kAcceptFriendResponse:
            return std::make_shared<AcceptFriendResponse>();
        case MessageType::kDeleteFriendRequest:
            return std::make_shared<DeleteFriendRequest>();
        case MessageType::kDeleteFriendResponse:
            return std::make_shared<DeleteFriendResponse>();
        case MessageType::kFriendListRequest:
            return std::make_shared<FriendListRequest>();
        case MessageType::kFriendListResponse:
            return std::make_shared<FriendListResponse>();
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
    root["msg_id"] = static_cast<Json::UInt>(msg_id);
    root["client_msg_id"] = static_cast<Json::UInt>(client_msg_id);
    root["from_user_id"] = static_cast<Json::UInt>(from_user_id);
    root["from_username"] = from_username;
    root["to_user_id"] = static_cast<Json::UInt>(to_user_id);
    root["content"] = content;
    root["timestamp"] = static_cast<Json::Int64>(timestamp);
    root["created_at"] = created_at;
    return root;
}

bool PrivateMessage::FromJson(const Json::Value& root) {
    if (!root.isMember("content"))
        return false;
    msg_id = root.get("msg_id", 0).asUInt();
    client_msg_id = root.get("client_msg_id", 0).asUInt();
    from_user_id = root.get("from_user_id", 0).asUInt();
    from_username = root.get("from_username", "").asString();
    to_user_id = root.get("to_user_id", 0).asUInt();
    content = root["content"].asString();
    timestamp = root.get("timestamp", 0).asInt64();
    created_at = root.get("created_at", "").asString();
    return true;
}

// ============================================================
// PrivateMessageAck
// ============================================================

Json::Value PrivateMessageAck::ToJson() const {
    Json::Value root;
    root["type"] = "private_message_ack";
    root["client_msg_id"] = static_cast<Json::UInt>(client_msg_id);
    root["msg_id"] = static_cast<Json::UInt>(msg_id);
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool PrivateMessageAck::FromJson(const Json::Value& root) {
    client_msg_id = root.get("client_msg_id", 0).asUInt();
    msg_id = root.get("msg_id", 0).asUInt();
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// PrivateHistoryRequest
// ============================================================

Json::Value PrivateHistoryRequest::ToJson() const {
    Json::Value root;
    root["type"] = "private_history_request";
    root["friend_id"] = static_cast<Json::UInt>(friend_id);
    root["after_id"] = static_cast<Json::UInt>(after_id);
    root["limit"] = limit;
    return root;
}

bool PrivateHistoryRequest::FromJson(const Json::Value& root) {
    friend_id = root.get("friend_id", 0).asUInt();
    after_id = root.get("after_id", 0).asUInt();
    limit = root.get("limit", 50).asInt();
    return true;
}

// ============================================================
// PrivateHistoryResponse
// ============================================================

Json::Value PrivateHistoryResponse::ToJson() const {
    Json::Value root;
    root["type"] = "private_history_response";
    root["success"] = success;
    root["message"] = message;
    root["friend_id"] = static_cast<Json::UInt>(friend_id);

    Json::Value arr(Json::arrayValue);
    for (const auto& m : messages) {
        Json::Value item;
        item["msg_id"] = static_cast<Json::UInt>(m.msg_id);
        item["from_user_id"] = static_cast<Json::UInt>(m.from_user_id);
        item["to_user_id"] = static_cast<Json::UInt>(m.to_user_id);
        item["content"] = m.content;
        item["is_read"] = m.is_read;
        item["is_revoked"] = m.is_revoked;
        item["reply_to_id"] = static_cast<Json::UInt>(m.reply_to_id);
        item["reply_preview"] = m.reply_preview;
        item["created_at"] = m.created_at;
        item["updated_at"] = m.updated_at;
        arr.append(item);
    }
    root["messages"] = arr;
    return root;
}

bool PrivateHistoryResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    friend_id = root.get("friend_id", 0).asUInt();

    const Json::Value& arr = root["messages"];
    if (arr.isArray()) {
        for (const auto& item : arr) {
            Item m;
            m.msg_id = item.get("msg_id", 0).asUInt();
            m.from_user_id = item.get("from_user_id", 0).asUInt();
            m.to_user_id = item.get("to_user_id", 0).asUInt();
            m.content = item.get("content", "").asString();
            m.is_read = item.get("is_read", false).asBool();
            m.is_revoked = item.get("is_revoked", false).asBool();
            m.reply_to_id = item.get("reply_to_id", 0).asUInt();
            m.reply_preview = item.get("reply_preview", "").asString();
            m.created_at = item.get("created_at", "").asString();
            m.updated_at = item.get("updated_at", "").asString();
            messages.push_back(std::move(m));
        }
    }
    return true;
}

// ============================================================
// UnreadRequest / UnreadResponse
// ============================================================

Json::Value UnreadRequest::ToJson() const {
    Json::Value root;
    root["type"] = "unread_request";
    return root;
}

bool UnreadRequest::FromJson(const Json::Value& root) {
    (void)root;
    return true;
}

Json::Value UnreadResponse::ToJson() const {
    Json::Value root;
    root["type"] = "unread_response";
    root["success"] = success;
    root["total"] = total;
    Json::Value obj(Json::objectValue);
    for (const auto& kv : counts) {
        obj[std::to_string(kv.first)] = kv.second;
    }
    root["counts"] = obj;
    return root;
}

bool UnreadResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    total = root.get("total", 0).asInt();
    counts.clear();
    const Json::Value& obj = root["counts"];
    if (obj.isObject()) {
        for (const auto& name : obj.getMemberNames()) {
            try {
                counts[static_cast<uint32_t>(std::stoul(name))] = obj[name].asInt();
            } catch (...) {
                // 忽略非法 key，避免坏包导致解析失败
            }
        }
    }
    return true;
}

// ============================================================
// MarkReadRequest / MarkReadResponse
// ============================================================

Json::Value MarkReadRequest::ToJson() const {
    Json::Value root;
    root["type"] = "mark_read_request";
    root["friend_id"] = static_cast<Json::UInt>(friend_id);
    return root;
}

bool MarkReadRequest::FromJson(const Json::Value& root) {
    friend_id = root.get("friend_id", 0).asUInt();
    return true;
}

Json::Value MarkReadResponse::ToJson() const {
    Json::Value root;
    root["type"] = "mark_read_response";
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool MarkReadResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
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
// SearchUserRequest
// ============================================================

Json::Value SearchUserRequest::ToJson() const {
    Json::Value root;
    root["type"] = "search_user_request";
    root["keyword"] = keyword;
    return root;
}

bool SearchUserRequest::FromJson(const Json::Value& root) {
    keyword = root.get("keyword", "").asString();
    return !keyword.empty();
}

// ============================================================
// SearchUserResponse
// ============================================================

Json::Value SearchUserResponse::ToJson() const {
    Json::Value root;
    root["type"] = "search_user_response";
    root["success"] = success;
    Json::Value arr(Json::arrayValue);
    for (const auto& u : users) {
        Json::Value item;
        item["id"] = static_cast<Json::UInt>(u.id);
        item["email"] = u.email;
        item["username"] = u.username;
        item["avatar"] = u.avatar;
        arr.append(item);
    }
    root["users"] = arr;
    return root;
}

bool SearchUserResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    const Json::Value& arr = root["users"];
    if (arr.isArray()) {
        for (auto& v : arr) {
            UserItem u;
            u.id = v.get("id", 0).asUInt();
            u.email = v.get("email", "").asString();
            u.username = v.get("username", "").asString();
            u.avatar = v.get("avatar", "").asString();
            users.push_back(u);
        }
    }
    return true;
}

// ============================================================
// AddFriendRequest
// ============================================================

Json::Value AddFriendRequest::ToJson() const {
    Json::Value root;
    root["type"] = "add_friend_request";
    root["to_user_id"] = static_cast<Json::UInt>(to_user_id);
    return root;
}

bool AddFriendRequest::FromJson(const Json::Value& root) {
    to_user_id = root.get("to_user_id", 0).asUInt();
    return to_user_id > 0;
}

// ============================================================
// AddFriendResponse
// ============================================================

Json::Value AddFriendResponse::ToJson() const {
    Json::Value root;
    root["type"] = "add_friend_response";
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool AddFriendResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// AcceptFriendRequest
// ============================================================

Json::Value AcceptFriendRequest::ToJson() const {
    Json::Value root;
    root["type"] = "accept_friend_request";
    root["request_id"] = static_cast<Json::UInt>(request_id);
    return root;
}

bool AcceptFriendRequest::FromJson(const Json::Value& root) {
    request_id = root.get("request_id", 0).asUInt();
    return request_id > 0;
}

// ============================================================
// AcceptFriendResponse
// ============================================================

Json::Value AcceptFriendResponse::ToJson() const {
    Json::Value root;
    root["type"] = "accept_friend_response";
    root["success"] = success;
    root["message"] = message;
    if (success) {
        root["friend_id"] = static_cast<Json::UInt>(friend_id);
        root["friend_email"] = friend_email;
        root["friend_username"] = friend_username;
        root["friend_avatar"] = friend_avatar;
    }
    return root;
}

bool AcceptFriendResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    friend_id = root.get("friend_id", 0).asUInt();
    friend_email = root.get("friend_email", "").asString();
    friend_username = root.get("friend_username", "").asString();
    friend_avatar = root.get("friend_avatar", "").asString();
    return true;
}

// ============================================================
// DeleteFriendRequest
// ============================================================

Json::Value DeleteFriendRequest::ToJson() const {
    Json::Value root;
    root["type"] = "delete_friend_request";
    root["friend_id"] = static_cast<Json::UInt>(friend_id);
    return root;
}

bool DeleteFriendRequest::FromJson(const Json::Value& root) {
    friend_id = root.get("friend_id", 0).asUInt();
    return friend_id > 0;
}

// ============================================================
// DeleteFriendResponse
// ============================================================

Json::Value DeleteFriendResponse::ToJson() const {
    Json::Value root;
    root["type"] = "delete_friend_response";
    root["success"] = success;
    root["message"] = message;
    return root;
}

bool DeleteFriendResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    return true;
}

// ============================================================
// FriendListRequest
// ============================================================

Json::Value FriendListRequest::ToJson() const {
    Json::Value root;
    root["type"] = "friend_list_request";
    return root;
}

bool FriendListRequest::FromJson(const Json::Value& root) {
    (void)root;
    return true;
}

// ============================================================
// FriendListResponse
// ============================================================

Json::Value FriendListResponse::ToJson() const {
    Json::Value root;
    root["type"] = "friend_list_response";
    root["success"] = success;
    Json::Value arr(Json::arrayValue);
    for (const auto& f : friends) {
        Json::Value item;
        item["id"] = static_cast<Json::UInt>(f.id);
        item["email"] = f.email;
        item["username"] = f.username;
        item["avatar"] = f.avatar;
        item["remark"] = f.remark;
        item["online"] = f.online;
        arr.append(item);
    }
    root["friends"] = arr;
    return root;
}

bool FriendListResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    const Json::Value& arr = root["friends"];
    if (arr.isArray()) {
        for (auto& v : arr) {
            FriendItem f;
            f.id = v.get("id", 0).asUInt();
            f.email = v.get("email", "").asString();
            f.username = v.get("username", "").asString();
            f.avatar = v.get("avatar", "").asString();
            f.remark = v.get("remark", "").asString();
            f.online = v.get("online", false).asBool();
            friends.push_back(f);
        }
    }
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

// ============================================================
// SyncRequest / SyncResponse
// ============================================================

Json::Value SyncRequest::ToJson() const {
    Json::Value root;
    root["type"] = "sync_request";
    root["last_private_id"] = static_cast<Json::UInt>(last_private_id);
    root["limit"] = limit;
    return root;
}

bool SyncRequest::FromJson(const Json::Value& root) {
    last_private_id = root.get("last_private_id", 0).asUInt();
    limit = root.get("limit", 200).asInt();
    return true;
}

Json::Value SyncResponse::ToJson() const {
    Json::Value root;
    root["type"] = "sync_response";
    root["success"] = success;
    root["message"] = message;
    root["last_msg_id"] = static_cast<Json::UInt>(last_msg_id);
    root["has_more"] = has_more;

    Json::Value arr(Json::arrayValue);
    for (const auto& m : messages) {
        arr.append(m.ToJson());
    }
    root["messages"] = arr;
    return root;
}

bool SyncResponse::FromJson(const Json::Value& root) {
    success = root.get("success", false).asBool();
    message = root.get("message", "").asString();
    last_msg_id = root.get("last_msg_id", 0).asUInt();
    has_more = root.get("has_more", false).asBool();
    messages.clear();

    const Json::Value& arr = root["messages"];
    if (arr.isArray()) {
        for (const auto& item : arr) {
            PrivateMessage m;
            if (m.FromJson(item)) {
                messages.push_back(std::move(m));
            }
        }
    }
    return true;
}

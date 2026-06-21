#pragma once

/*
    协议消息类型定义
    聊天室所有消息类型的枚举
*/

#include <cstdint>

enum class MessageType : uint16_t {
    // 心跳
    kHeartbeat = 0,

    // 登录相关
    kLoginRequest = 1,
    kLoginResponse = 2,

    // 注册相关（含验证码）
    kRegisterRequest = 3,
    kRegisterResponse = 4,

    // 发送验证码
    kSendCodeRequest = 5,
    kSendCodeResponse = 6,

    // 聊天消息
    kChatMessage = 10,
    kPrivateMessage = 11,

    // 系统消息
    kSystemMessage = 20,

    // 登出
    kLogoutRequest = 30,
    kLogoutResponse = 31,

    // 错误
    kError = 99,

    // 未知
    kUnknown = 65535
};

// 获取消息类型名称（用于日志/调试）
inline const char* MessageTypeName(MessageType type) {
    switch (type) {
        case MessageType::kHeartbeat:        return "Heartbeat";
        case MessageType::kLoginRequest:     return "LoginRequest";
        case MessageType::kLoginResponse:    return "LoginResponse";
        case MessageType::kRegisterRequest:  return "RegisterRequest";
        case MessageType::kRegisterResponse: return "RegisterResponse";
        case MessageType::kSendCodeRequest:  return "SendCodeRequest";
        case MessageType::kSendCodeResponse: return "SendCodeResponse";
        case MessageType::kChatMessage:      return "ChatMessage";
        case MessageType::kPrivateMessage:   return "PrivateMessage";
        case MessageType::kSystemMessage:    return "SystemMessage";
        case MessageType::kLogoutRequest:    return "LogoutRequest";
        case MessageType::kLogoutResponse:   return "LogoutResponse";
        case MessageType::kError:            return "Error";
        default:                             return "Unknown";
    }
}

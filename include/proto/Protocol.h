#pragma once

/*
    协议格式定义

    网络字节序（大端）：
    ┌──────────────┬──────────────┬──────────────────┐
    │ body_length  │  msg_type    │   json_body      │
    │  (4 bytes)   │  (2 bytes)   │  (N bytes)       │
    │  uint32_t    │  uint16_t    │  UTF-8 JSON      │
    └──────────────┴──────────────┴──────────────────┘

    Header = 6 bytes，body_length 仅指 JSON body 的长度（不含 header）
*/

#include <cstdint>
#include <cstddef>
#include <string>
#include <cstring>
#include <arpa/inet.h>

namespace proto {

// 协议头大小：4 (body_len) + 2 (msg_type) = 6 bytes
inline constexpr size_t kHeaderSize = 6;
inline constexpr size_t kMaxBodySize = 64 * 1024;  // 64KB 上限，防止恶意包

// 协议头结构体
struct ProtocolHeader {
    uint32_t body_length;   // JSON body 长度（不含 header）
    uint16_t msg_type;      // 消息类型

    ProtocolHeader() : body_length(0), msg_type(0) {}

    // 编码为网络字节序的 6 字节数据
    std::string Encode() const {
        uint32_t net_len = htonl(body_length);
        uint16_t net_type = htons(msg_type);
        std::string result(kHeaderSize, '\0');
        std::memcpy(&result[0], &net_len, sizeof(net_len));
        std::memcpy(&result[4], &net_type, sizeof(net_type));
        return result;
    }

    // 从网络字节序的原始数据解码
    // 返回 true 表示解码成功，false 表示数据不足
    static bool Decode(const char* data, size_t len, ProtocolHeader& header) {
        if (len < kHeaderSize)
            return false;

        uint32_t net_len;
        uint16_t net_type;
        std::memcpy(&net_len, data, sizeof(net_len));
        std::memcpy(&net_type, data + 4, sizeof(net_type));

        header.body_length = ntohl(net_len);
        header.msg_type = ntohs(net_type);
        return true;
    }
};

}  // namespace proto

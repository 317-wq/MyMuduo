#pragma once

/*
    协议编解码器（Codec）

    职责：
    1. Encode — 将 Message 编码为 "6字节协议头 + JSON body" 的字符串
    2. Decode — 从 Buffer 中尝试提取完整消息，处理半包/粘包

    用法：
    - 服务端/客户端在 OnMessage 回调中调用 Codec::OnMessage，当解析出完整
      消息时会回调用户设置的 MessageCallback
    - 发送消息时调用 Codec::Encode 得到编码后的字符串，再通过 TcpConnection::Send 发送
*/

#include "net/Buffer.h"
#include "proto/Message.h"
#include "proto/Protocol.h"
#include "net/TcpConnection.h"

#include <functional>
#include <vector>
#include <string>
#include <memory>

class Codec {
public:
    using MessageCallback = std::function<void(const TcpConnection::Ptr&, Message::Ptr)>;

    Codec();

    // 设置收到完整消息时的回调
    void SetMessageCallback(MessageCallback cb) { _message_cb = std::move(cb); }

    // 将 Message 编码为网络传输格式（header + JSON body）
    std::string Encode(const Message& msg);

    // 当连接上有数据到达时调用此方法
    // 它会从 Buffer 中尝试提取完整消息，每提取出一条就回调一次
    void OnMessage(const TcpConnection::Ptr& conn, Buffer* buf);

private:
    MessageCallback _message_cb;
};

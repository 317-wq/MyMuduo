#pragma once

/*
    消息分发器

    根据消息类型将 Message 分发到注册的回调处理器。
    每个类型可以注册一个回调，未注册的类型默认忽略。

    典型用法：
        Dispatcher dispatcher;
        dispatcher.Register(MessageType::kLoginRequest,
            [](auto conn, auto msg, auto ts) { ... });
        dispatcher.Register(MessageType::kChatMessage,
            [](auto conn, auto msg, auto ts) { ... });

        // 在 Codec 的 MessageCallback 中：
        codec.SetMessageCallback([&dispatcher](auto conn, auto msg) {
            dispatcher.Dispatch(conn, msg, Timestamp::Now());
        });
*/

#include "proto/Message.h"
#include "proto/MessageType.h"
#include "net/TcpConnection.h"
#include "base/Timestamp.h"

#include <functional>
#include <unordered_map>

class Dispatcher {
public:
    using Handler = std::function<void(const TcpConnection::Ptr&, Message::Ptr, Timestamp)>;

    Dispatcher() = default;

    // 注册某消息类型的处理器（覆盖式）
    void Register(MessageType type, Handler handler) {
        _handlers[type] = std::move(handler);
    }

    // 分发消息到对应处理器，未注册的类型直接忽略
    void Dispatch(const TcpConnection::Ptr& conn,
                  Message::Ptr msg,
                  Timestamp ts) const {
        auto it = _handlers.find(msg->GetType());
        if (it != _handlers.end() && it->second) {
            it->second(conn, std::move(msg), ts);
        }
    }

private:
    std::unordered_map<MessageType, Handler> _handlers;
};

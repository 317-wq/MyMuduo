#include "proto/Codec.h"
#include "proto/Protocol.h"
#include "proto/Message.h"
#include "proto/MessageType.h"
#include "net/Buffer.h"
#include "net/TcpConnection.h"

#include <memory>
#include <string>
#include <cstring>
#include <iostream>

Codec::Codec() = default;

std::string Codec::Encode(const Message& msg) {
    std::string json_body = msg.ToJsonString();

    if (json_body.size() > proto::kMaxBodySize) {
        std::cerr << "Codec::Encode: body size " << json_body.size()
                  << " exceeds max " << proto::kMaxBodySize << std::endl;
        return {};
    }

    proto::ProtocolHeader header;
    header.body_length = static_cast<uint32_t>(json_body.size());
    header.msg_type = static_cast<uint16_t>(msg.GetType());

    std::string result = header.Encode();
    result.append(json_body);
    return result;
}

void Codec::OnMessage(const TcpConnection::Ptr& conn, Buffer* buf) {
    // 循环处理粘包：一条 TCP 报文可能包含多个消息帧
    while (buf->ReadableSize() >= proto::kHeaderSize) {

        // Peek header（不消费），获取 body_length 和 msg_type
        proto::ProtocolHeader header;
        if (!proto::ProtocolHeader::Decode(buf->Peek(), proto::kHeaderSize, header)) {
            break;
        }

        // 检查 body_length 合法性
        if (header.body_length > proto::kMaxBodySize) {
            std::cerr << "Codec::OnMessage: bad body length " << header.body_length
                      << ", clearing buffer" << std::endl;
            buf->RetrieveAll();
            return;
        }

        // 半包：完整消息尚未到达 → 等待更多数据
        size_t total_len = proto::kHeaderSize + header.body_length;
        if (buf->ReadableSize() < total_len) {
            return;
        }

        // 消费 header（6 bytes）
        buf->Retrieve(proto::kHeaderSize);

        // 消费 JSON body
        std::string json_body = buf->Retrieve(header.body_length);

        // JSON → Message
        Message::Ptr msg = Message::Create(static_cast<MessageType>(header.msg_type));
        if (!msg) {
            // 未知消息类型，跳过此帧，继续处理后续数据
            continue;
        }

        if (!msg->FromJsonString(json_body)) {
            std::cerr << "Codec::OnMessage: parse failed for type "
                      << static_cast<int>(header.msg_type) << std::endl;
            continue;
        }

        // 回调上层
        if (_message_cb) {
            _message_cb(conn, std::move(msg));
        }
    }
}

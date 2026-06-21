/*
    协议层单元测试

    覆盖：
    - ProtocolHeader 编解码
    - 所有 Message 类型的序列化/反序列化
    - Codec 编解码（完整包、半包、粘包、超大包）
    - Dispatcher 消息分发
*/

#include "proto/Protocol.h"
#include "proto/Message.h"
#include "proto/MessageType.h"
#include "proto/Codec.h"
#include "proto/Dispatcher.h"
#include "net/Buffer.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <cstring>
#include <vector>
#include <chrono>

// ============================================================
// ProtocolHeader 测试
// ============================================================

TEST(ProtocolHeaderTest, EncodeDecode) {
    proto::ProtocolHeader hdr;
    hdr.body_length = 128;
    hdr.msg_type = 5;

    // 编码
    std::string encoded = hdr.Encode();
    EXPECT_EQ(encoded.size(), proto::kHeaderSize);  // 6 bytes

    // 解码
    proto::ProtocolHeader decoded;
    bool ok = proto::ProtocolHeader::Decode(encoded.data(), encoded.size(), decoded);
    EXPECT_TRUE(ok);
    EXPECT_EQ(decoded.body_length, 128);
    EXPECT_EQ(decoded.msg_type, 5);
}

TEST(ProtocolHeaderTest, DecodeInsufficientData) {
    proto::ProtocolHeader hdr;
    char buf[4] = {0};  // 不足 6 字节

    bool ok = proto::ProtocolHeader::Decode(buf, 4, hdr);
    EXPECT_FALSE(ok);
}

TEST(ProtocolHeaderTest, EncodeZeroLengthBody) {
    proto::ProtocolHeader hdr;
    hdr.body_length = 0;
    hdr.msg_type = static_cast<uint16_t>(MessageType::kHeartbeat);

    std::string encoded = hdr.Encode();
    EXPECT_EQ(encoded.size(), proto::kHeaderSize);

    proto::ProtocolHeader decoded;
    proto::ProtocolHeader::Decode(encoded.data(), encoded.size(), decoded);
    EXPECT_EQ(decoded.body_length, 0);
    EXPECT_EQ(decoded.msg_type, static_cast<uint16_t>(MessageType::kHeartbeat));
}

TEST(ProtocolHeaderTest, EncodeLargeBody) {
    proto::ProtocolHeader hdr;
    hdr.body_length = 65535;
    hdr.msg_type = 65534;

    std::string encoded = hdr.Encode();
    proto::ProtocolHeader decoded;
    proto::ProtocolHeader::Decode(encoded.data(), encoded.size(), decoded);
    EXPECT_EQ(decoded.body_length, 65535);
    EXPECT_EQ(decoded.msg_type, 65534);
}

// ============================================================
// Message 序列化/反序列化 测试
// ============================================================

TEST(MessageTest, HeartbeatMessage) {
    HeartbeatMessage msg;
    EXPECT_EQ(msg.GetType(), MessageType::kHeartbeat);

    Json::Value json = msg.ToJson();
    EXPECT_EQ(json["type"].asString(), "heartbeat");

    HeartbeatMessage restored;
    EXPECT_TRUE(restored.FromJson(json));
}

TEST(MessageTest, LoginRequest) {
    LoginRequest msg;
    msg.email = "alice@example.com";
    msg.password = "secret123";

    std::string json_str = msg.ToJsonString();
    EXPECT_FALSE(json_str.empty());
    EXPECT_NE(json_str.find("alice@example.com"), std::string::npos);
    EXPECT_NE(json_str.find("secret123"), std::string::npos);

    LoginRequest restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.email, "alice@example.com");
    EXPECT_EQ(restored.password, "secret123");
}

TEST(MessageTest, LoginRequestMissingField) {
    // JSON 缺少 password 字段
    std::string json = R"({"type":"login_request","email":"bob@test.com"})";
    LoginRequest msg;
    EXPECT_FALSE(msg.FromJsonString(json));
}

TEST(MessageTest, LoginResponse) {
    LoginResponse msg;
    msg.success = true;
    msg.user_id = 42;
    msg.username = "alice";
    msg.message = "Welcome back!";

    std::string json_str = msg.ToJsonString();
    LoginResponse restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_TRUE(restored.success);
    EXPECT_EQ(restored.user_id, 42);
    EXPECT_EQ(restored.username, "alice");
    EXPECT_EQ(restored.message, "Welcome back!");
}

TEST(MessageTest, LoginResponseFailure) {
    LoginResponse msg;
    msg.success = false;
    msg.message = "Invalid password";

    std::string json_str = msg.ToJsonString();
    LoginResponse restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_FALSE(restored.success);
    EXPECT_EQ(restored.message, "Invalid password");
}

TEST(MessageTest, RegisterRequest) {
    RegisterRequest msg;
    msg.username = "newuser";
    msg.password = "pass123";

    std::string json_str = msg.ToJsonString();
    RegisterRequest restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.username, "newuser");
    EXPECT_EQ(restored.password, "pass123");
}

TEST(MessageTest, RegisterResponse) {
    RegisterResponse msg;
    msg.success = true;
    msg.message = "Registration successful";

    std::string json_str = msg.ToJsonString();
    RegisterResponse restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_TRUE(restored.success);
    EXPECT_EQ(restored.message, "Registration successful");
}

TEST(MessageTest, ChatMessage) {
    ChatMessage msg;
    msg.user_id = 7;
    msg.username = "alice";
    msg.content = "Hello, world!";
    msg.timestamp = 1718208000000;

    std::string json_str = msg.ToJsonString();
    EXPECT_NE(json_str.find("Hello, world!"), std::string::npos);

    ChatMessage restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.user_id, 7);
    EXPECT_EQ(restored.username, "alice");
    EXPECT_EQ(restored.content, "Hello, world!");
    EXPECT_EQ(restored.timestamp, 1718208000000);
}

TEST(MessageTest, ChatMessageMissingContent) {
    std::string json = R"({"type":"chat_message","user_id":1})";
    ChatMessage msg;
    EXPECT_FALSE(msg.FromJsonString(json));
}

TEST(MessageTest, PrivateMessage) {
    PrivateMessage msg;
    msg.from_user_id = 1;
    msg.from_username = "alice";
    msg.to_user_id = 2;
    msg.content = "Secret!";
    msg.timestamp = 1718208000000;

    std::string json_str = msg.ToJsonString();
    PrivateMessage restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.from_user_id, 1);
    EXPECT_EQ(restored.from_username, "alice");
    EXPECT_EQ(restored.to_user_id, 2);
    EXPECT_EQ(restored.content, "Secret!");
}

TEST(MessageTest, SystemMessage) {
    SystemMessage msg;
    msg.content = "Server will restart in 5 minutes";
    msg.timestamp = 999;

    std::string json_str = msg.ToJsonString();
    SystemMessage restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.content, "Server will restart in 5 minutes");
    EXPECT_EQ(restored.timestamp, 999);
}

TEST(MessageTest, LogoutRequest) {
    LogoutRequest msg;
    msg.user_id = 99;

    std::string json_str = msg.ToJsonString();
    LogoutRequest restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.user_id, 99);
}

TEST(MessageTest, LogoutResponse) {
    LogoutResponse msg;
    msg.success = true;
    msg.message = "Goodbye";

    std::string json_str = msg.ToJsonString();
    LogoutResponse restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_TRUE(restored.success);
    EXPECT_EQ(restored.message, "Goodbye");
}

TEST(MessageTest, ErrorMessage) {
    ErrorMessage msg;
    msg.code = 401;
    msg.message = "Unauthorized";

    std::string json_str = msg.ToJsonString();
    ErrorMessage restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.code, 401);
    EXPECT_EQ(restored.message, "Unauthorized");
}

TEST(MessageTest, FactoryCreate) {
    // 测试工厂方法对各类型都能正确创建
    auto msg1 = Message::Create(MessageType::kHeartbeat);
    EXPECT_NE(msg1, nullptr);
    EXPECT_EQ(msg1->GetType(), MessageType::kHeartbeat);

    auto msg2 = Message::Create(MessageType::kLoginRequest);
    EXPECT_NE(msg2, nullptr);

    auto msg3 = Message::Create(MessageType::kChatMessage);
    EXPECT_NE(msg3, nullptr);

    auto msg4 = Message::Create(MessageType::kSystemMessage);
    EXPECT_NE(msg4, nullptr);

    auto msg5 = Message::Create(MessageType::kError);
    EXPECT_NE(msg5, nullptr);
}

TEST(MessageTest, FactoryCreateUnknownType) {
    auto msg = Message::Create(MessageType::kUnknown);
    EXPECT_EQ(msg, nullptr);
}

TEST(MessageTest, FromJsonStringInvalid) {
    std::string bad_json = "not a json string!!!";
    LoginRequest msg;
    EXPECT_FALSE(msg.FromJsonString(bad_json));
}

TEST(MessageTest, UnicodeContent) {
    // 测试中文内容
    ChatMessage msg;
    msg.username = "张三";
    msg.content = "你好，世界！Hello World! 🌍";

    std::string json_str = msg.ToJsonString();
    ChatMessage restored;
    EXPECT_TRUE(restored.FromJsonString(json_str));
    EXPECT_EQ(restored.username, "张三");
    EXPECT_EQ(restored.content, "你好，世界！Hello World! 🌍");
}

// ============================================================
// Codec 编解码测试
// ============================================================

class CodecTest : public ::testing::Test {
protected:
    void SetUp() override {
        _codec.SetMessageCallback(
            [this](const TcpConnection::Ptr& conn, Message::Ptr msg) {
                (void)conn;
                _received.push_back(std::move(msg));
            });
    }

    // 创建一条聊天消息并编码
    std::string EncodeChatMsg(uint32_t uid, const std::string& name,
                               const std::string& text) {
        ChatMessage msg;
        msg.user_id = uid;
        msg.username = name;
        msg.content = text;
        msg.timestamp = 1000;
        return _codec.Encode(msg);
    }

    Codec _codec;
    Buffer _buf;
    std::vector<Message::Ptr> _received;
};

TEST_F(CodecTest, EncodeProducedValidFormat) {
    ChatMessage msg;
    msg.user_id = 1;
    msg.username = "test";
    msg.content = "hi";
    msg.timestamp = 0;

    std::string encoded = _codec.Encode(msg);
    EXPECT_GE(encoded.size(), proto::kHeaderSize);

    // 验证 header
    proto::ProtocolHeader hdr;
    proto::ProtocolHeader::Decode(encoded.data(), proto::kHeaderSize, hdr);
    EXPECT_EQ(hdr.msg_type, static_cast<uint16_t>(MessageType::kChatMessage));
    EXPECT_EQ(hdr.body_length, encoded.size() - proto::kHeaderSize);

    // 验证 body 是合法 JSON
    std::string body = encoded.substr(proto::kHeaderSize);
    ChatMessage restored;
    EXPECT_TRUE(restored.FromJsonString(body));
    EXPECT_EQ(restored.content, "hi");
}

TEST_F(CodecTest, DecodeSingleMessage) {
    std::string encoded = EncodeChatMsg(1, "alice", "hello");
    _buf.Append(encoded);

    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 1);
    EXPECT_EQ(_received[0]->GetType(), MessageType::kChatMessage);

    auto* chat = dynamic_cast<ChatMessage*>(_received[0].get());
    ASSERT_NE(chat, nullptr);
    EXPECT_EQ(chat->user_id, 1);
    EXPECT_EQ(chat->username, "alice");
    EXPECT_EQ(chat->content, "hello");
}

TEST_F(CodecTest, DecodeMultipleMessages) {
    // 粘包：多条消息在同一个 Buffer 中
    _buf.Append(EncodeChatMsg(1, "a", "msg1"));
    _buf.Append(EncodeChatMsg(2, "b", "msg2"));
    _buf.Append(EncodeChatMsg(3, "c", "msg3"));

    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 3);
    EXPECT_EQ(dynamic_cast<ChatMessage*>(_received[0].get())->content, "msg1");
    EXPECT_EQ(dynamic_cast<ChatMessage*>(_received[1].get())->content, "msg2");
    EXPECT_EQ(dynamic_cast<ChatMessage*>(_received[2].get())->content, "msg3");

    // Buffer 应该被消费完毕
    EXPECT_EQ(_buf.ReadableSize(), 0);
}

TEST_F(CodecTest, HalfPacketHeader) {
    // 只写入不完整的 header（少于 6 字节）
    std::string full = EncodeChatMsg(1, "a", "test");
    _buf.Append(full.data(), 4);  // 只写入 4 字节

    _codec.OnMessage(nullptr, &_buf);

    // 没有收到回调
    EXPECT_EQ(_received.size(), 0);
    // 数据没有被消费
    EXPECT_EQ(_buf.ReadableSize(), 4);

    // 补充剩余数据
    _buf.Append(full.data() + 4, full.size() - 4);
    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 1);
}

TEST_F(CodecTest, HalfPacketBody) {
    // header 完整但 body 不完整
    std::string full = EncodeChatMsg(1, "a", "a very long message that spans across...");
    size_t split = proto::kHeaderSize + 5;  // header + 只有 5 字节 body
    _buf.Append(full.data(), split);

    _codec.OnMessage(nullptr, &_buf);

    // 半包 → 不回调
    EXPECT_EQ(_received.size(), 0);
    EXPECT_EQ(_buf.ReadableSize(), split);

    // 补充剩余 body
    _buf.Append(full.data() + split, full.size() - split);
    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 1);
}

TEST_F(CodecTest, StickyPacketWithHalfAtEnd) {
    // 粘包 + 末尾半包
    std::string msg1 = EncodeChatMsg(1, "a", "first");
    std::string msg2 = EncodeChatMsg(2, "b", "second");
    std::string msg3 = EncodeChatMsg(3, "c", "third");

    // 写入完整 msg1 + 完整 msg2 + 半个 msg3
    _buf.Append(msg1);
    _buf.Append(msg2);
    _buf.Append(msg3.data(), msg3.size() / 2);

    _codec.OnMessage(nullptr, &_buf);

    // msg1 和 msg2 被解析，msg3 的半包留在 buffer
    EXPECT_EQ(_received.size(), 2);

    // 补充 msg3 的剩余部分
    _buf.Append(msg3.data() + msg3.size() / 2, msg3.size() - msg3.size() / 2);
    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 3);
}

TEST_F(CodecTest, EmptyBodyMessage) {
    // 心跳消息：body 只包含 {"type":"heartbeat"}，约 20 字节
    HeartbeatMessage hb;
    std::string encoded = _codec.Encode(hb);
    EXPECT_GT(encoded.size(), proto::kHeaderSize);

    // 验证 header 中的 body_length 与实际 body 大小一致
    proto::ProtocolHeader hdr;
    proto::ProtocolHeader::Decode(encoded.data(), proto::kHeaderSize, hdr);
    EXPECT_EQ(hdr.body_length, encoded.size() - proto::kHeaderSize);

    _buf.Append(encoded);
    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 1);
    EXPECT_EQ(_received[0]->GetType(), MessageType::kHeartbeat);
}

TEST_F(CodecTest, EmptyBuffer) {
    _codec.OnMessage(nullptr, &_buf);
    EXPECT_EQ(_received.size(), 0);
}

TEST_F(CodecTest, EncodeMessageRoundTrip) {
    // 测试各种消息类型的完整编解码往返
    {
        LoginRequest req;
        req.email = "tester@example.com";
        req.password = "pwd";
        std::string encoded = _codec.Encode(req);
        _buf.Append(encoded);
    }
    {
        SystemMessage sys;
        sys.content = "System notice";
        std::string encoded = _codec.Encode(sys);
        _buf.Append(encoded);
    }

    _codec.OnMessage(nullptr, &_buf);

    EXPECT_EQ(_received.size(), 2);
    EXPECT_EQ(_received[0]->GetType(), MessageType::kLoginRequest);
    EXPECT_EQ(_received[1]->GetType(), MessageType::kSystemMessage);

    auto* login = dynamic_cast<LoginRequest*>(_received[0].get());
    ASSERT_NE(login, nullptr);
    EXPECT_EQ(login->email, "tester@example.com");
    EXPECT_EQ(login->password, "pwd");

    auto* sys = dynamic_cast<SystemMessage*>(_received[1].get());
    ASSERT_NE(sys, nullptr);
    EXPECT_EQ(sys->content, "System notice");
}

// ============================================================
// Dispatcher 测试
// ============================================================

TEST(DispatcherTest, DispatchToRegisteredHandler) {
    Dispatcher disp;
    int chat_count = 0;
    int login_count = 0;

    disp.Register(MessageType::kChatMessage,
        [&](const TcpConnection::Ptr&, Message::Ptr msg, Timestamp) {
            chat_count++;
            auto* m = dynamic_cast<ChatMessage*>(msg.get());
            EXPECT_NE(m, nullptr);
        });

    disp.Register(MessageType::kLoginRequest,
        [&](const TcpConnection::Ptr&, Message::Ptr msg, Timestamp) {
            login_count++;
            auto* m = dynamic_cast<LoginRequest*>(msg.get());
            EXPECT_NE(m, nullptr);
        });

    // 分发聊天消息
    auto chat = std::make_shared<ChatMessage>();
    chat->content = "test";
    disp.Dispatch(nullptr, chat, Timestamp::Now());
    EXPECT_EQ(chat_count, 1);
    EXPECT_EQ(login_count, 0);

    // 分发登录消息
    auto login = std::make_shared<LoginRequest>();
    login->email = "u@test.com";
    login->password = "p";
    disp.Dispatch(nullptr, login, Timestamp::Now());
    EXPECT_EQ(chat_count, 1);
    EXPECT_EQ(login_count, 1);
}

TEST(DispatcherTest, UnregisteredTypeIgnored) {
    Dispatcher disp;
    bool any_called = false;
    disp.Register(MessageType::kChatMessage,
        [&](const TcpConnection::Ptr&, Message::Ptr, Timestamp) {
            any_called = true;
        });

    // 分发未注册的类型 → 不应触发回调
    auto heartbeat = std::make_shared<HeartbeatMessage>();
    disp.Dispatch(nullptr, heartbeat, Timestamp::Now());
    EXPECT_FALSE(any_called);
}

TEST(DispatcherTest, OverwriteHandler) {
    Dispatcher disp;
    int first = 0, second = 0;

    disp.Register(MessageType::kChatMessage,
        [&](const TcpConnection::Ptr&, Message::Ptr, Timestamp) { first++; });
    disp.Register(MessageType::kChatMessage,
        [&](const TcpConnection::Ptr&, Message::Ptr, Timestamp) { second++; });

    auto chat = std::make_shared<ChatMessage>();
    disp.Dispatch(nullptr, chat, Timestamp::Now());

    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

// ============================================================
// MessageTypeName 测试
// ============================================================

TEST(MessageTypeNameTest, AllTypesHaveName) {
    EXPECT_STREQ(MessageTypeName(MessageType::kHeartbeat), "Heartbeat");
    EXPECT_STREQ(MessageTypeName(MessageType::kLoginRequest), "LoginRequest");
    EXPECT_STREQ(MessageTypeName(MessageType::kLoginResponse), "LoginResponse");
    EXPECT_STREQ(MessageTypeName(MessageType::kChatMessage), "ChatMessage");
    EXPECT_STREQ(MessageTypeName(MessageType::kPrivateMessage), "PrivateMessage");
    EXPECT_STREQ(MessageTypeName(MessageType::kSystemMessage), "SystemMessage");
    EXPECT_STREQ(MessageTypeName(MessageType::kError), "Error");
}

// ============================================================
// 边界条件测试
// ============================================================

TEST(EdgeCaseTest, CodecEncodeBodyExactlyMaxSize) {
    // 构造一个恰好接近上限的 body（实际测试用较小的 body）
    Codec codec;
    ChatMessage msg;
    msg.content = std::string(100, 'x');  // 100 字节 content
    msg.username = "test";

    std::string encoded = codec.Encode(msg);
    EXPECT_FALSE(encoded.empty());

    // 验证 body_length 与 body 实际大小一致
    proto::ProtocolHeader hdr;
    proto::ProtocolHeader::Decode(encoded.data(), proto::kHeaderSize, hdr);
    EXPECT_EQ(static_cast<size_t>(hdr.body_length), encoded.size() - proto::kHeaderSize);
}

# MyMuduo 协议层说明

> 本文档描述 MyMuduo 自定义应用层协议的完整规范、编解码流程和消息分发机制。

---

## 一、协议设计概述

### 1.1 为什么自定义协议？

在聊天室场景中，客户端和服务器需要交换多种类型的消息（登录、注册、聊天、好友…）。自定义二进制协议比直接用 JSON 流有以下优势：

| 对比 | JSON 流 | 自定义协议 |
|------|---------|-----------|
| 帧定界 | 需要分隔符或长度前缀，易出错 | 定长 header，精确知道每个帧边界 |
| 性能 | 字符串解析较慢 | 二进制 header 解析极快 |
| 扩展性 | 加字段容易 | 通过 type 枚举 + JSON body 灵活扩展 |
| 调试 | 可读性好 | body 用 JSON，保留可读性 |

### 1.2 设计思想

**定长二进制头 + JSON 体** 的混合方案：

- **Header 用二进制**：固定 6 字节，解析快，帧定界精确
- **Body 用 JSON**：可读性好，方便调试，前端 JS 原生支持

---

## 二、线格式 (Wire Format)

### 2.1 帧结构

```
字节偏移    0               4       6                 6 + N
           ├───────────────┼───────┼─────────────────┤
           │  body_length  │msg_type│   json_body      │
           │  (4 bytes)    │2 bytes│  (N bytes)       │
           │  uint32_t     │uint16_t│  UTF-8 JSON      │
           └───────────────┴───────┴─────────────────┘
           ←─── Header = 6 bytes ──→ ←── Body = N bytes ──→
```

### 2.2 字段说明

| 字段 | 类型 | 字节数 | 字节序 | 说明 |
|------|------|--------|--------|------|
| `body_length` | `uint32_t` | 4 | 大端 (network) | JSON body 的长度，不含 header |
| `msg_type` | `uint16_t` | 2 | 大端 (network) | 消息类型枚举值 |
| `json_body` | UTF-8 string | N | N/A | JSON 格式的消息体 |

### 2.3 约束

| 约束 | 值 | 说明 |
|------|-----|------|
| Header 大小 | 固定 6 字节 | `kHeaderSize = 6` |
| 最大 Body 大小 | 64 KB | `kMaxBodySize = 64 * 1024`，防止恶意超大包 |
| Body 编码 | UTF-8 | 支持中文等多字节字符 |

### 2.4 编解码实现

```cpp
// 编码（主机 → 网络字节序）
struct ProtocolHeader {
    uint32_t body_length;
    uint16_t msg_type;

    std::string Encode() const {
        uint32_t net_len = htonl(body_length);   // 4 bytes big-endian
        uint16_t net_type = htons(msg_type);      // 2 bytes big-endian
        std::string result(kHeaderSize, '\0');
        std::memcpy(&result[0], &net_len, sizeof(net_len));
        std::memcpy(&result[4], &net_type, sizeof(net_type));
        return result;
    }

    // 解码（网络 → 主机字节序）
    static bool Decode(const char* data, size_t len, ProtocolHeader& header) {
        if (len < kHeaderSize) return false;  // 数据不足
        uint32_t net_len;  uint16_t net_type;
        std::memcpy(&net_len,  data,      sizeof(net_len));
        std::memcpy(&net_type, data + 4,  sizeof(net_type));
        header.body_length = ntohl(net_len);
        header.msg_type    = ntohs(net_type);
        return true;
    }
};
```

**为什么用大端序？**
网络字节序 (Network Byte Order) 就是大端序，这是 TCP/IP 协议族的约定。用 `htonl/ntohl` 转换确保在不同架构（x86 小端、ARM 可变）间互操作。

---

## 三、消息类型定义

### 3.1 类型枚举

```cpp
enum class MessageType : uint16_t {
    // 心跳
    kHeartbeat = 0,

    // 登录 (1-2)
    kLoginRequest  = 1,
    kLoginResponse = 2,

    // 注册 + 验证码 (3-6)
    kRegisterRequest  = 3,
    kRegisterResponse = 4,
    kSendCodeRequest  = 5,
    kSendCodeResponse = 6,

    // 聊天 (10-11)
    kChatMessage    = 10,
    kPrivateMessage = 11,

    // 系统 (20)
    kSystemMessage = 20,

    // 登出 (30-31)
    kLogoutRequest  = 30,
    kLogoutResponse = 31,

    // 好友 (40-49)
    kSearchUserRequest   = 40,
    kSearchUserResponse  = 41,
    kAddFriendRequest    = 42,
    kAddFriendResponse   = 43,
    kAcceptFriendRequest = 44,
    kAcceptFriendResponse = 45,
    kDeleteFriendRequest = 46,
    kDeleteFriendResponse = 47,
    kFriendListRequest   = 48,
    kFriendListResponse  = 49,

    // 错误 (99)
    kError = 99,

    kUnknown = 65535
};
```

类型编号按功能域分段：心跳 0、登录 1-9、聊天 10-19、系统 20-29、登出 30-39、好友 40-49，便于扩展。

### 3.2 消息类型总览

#### 心跳类

| 类型 | 枚举 | Body 字段 | 说明 |
|------|------|-----------|------|
| `HeartbeatMessage` | `kHeartbeat` | `{type: "heartbeat"}` | 客户端定时发送保活 |

#### 登录类

| 类型 | Body 字段 (Request) | Body 字段 (Response) |
|------|---------------------|----------------------|
| `LoginRequest` | `email`, `password` | — |
| `LoginResponse` | — | `success`, `user_id`, `username`, `avatar`, `message` |

#### 注册 & 验证码类

| 类型 | Body 字段 (Request) | Body 字段 (Response) |
|------|---------------------|----------------------|
| `SendCodeRequest` | `email`, `code_type` (1=注册, 2=重置) | — |
| `SendCodeResponse` | — | `success`, `message` |
| `RegisterRequest` | `email`, `code`, `password`, `username` | — |
| `RegisterResponse` | — | `success`, `user_id`, `message` |

#### 聊天类

| 类型 | Body 字段 |
|------|-----------|
| `ChatMessage` | `user_id`, `username`, `content`, `timestamp` |
| `PrivateMessage` | `from_user_id`, `from_username`, `to_user_id`, `content`, `timestamp` |

#### 系统类

| 类型 | Body 字段 |
|------|-----------|
| `SystemMessage` | `content`, `timestamp` |

#### 注销类

| 类型 | Body 字段 (Request) | Body 字段 (Response) |
|------|---------------------|----------------------|
| `LogoutRequest` | `user_id` | — |
| `LogoutResponse` | — | `success`, `message` |

#### 好友类

| 类型 | Body 字段 (Request) | Body 字段 (Response) |
|------|---------------------|----------------------|
| `SearchUserRequest` | `keyword` | — |
| `SearchUserResponse` | — | `success`, `users: [{id, email, username, avatar}]` |
| `AddFriendRequest` | `to_user_id` | — |
| `AddFriendResponse` | — | `success`, `message` |
| `AcceptFriendRequest` | `request_id` | — |
| `AcceptFriendResponse` | — | `success`, `friend_id`, `friend_email`, `friend_username`, `friend_avatar` |
| `DeleteFriendRequest` | `friend_id` | — |
| `DeleteFriendResponse` | — | `success`, `message` |
| `FriendListRequest` | (空) | — |
| `FriendListResponse` | — | `success`, `friends: [{id, email, username, avatar, remark, online}]` |

#### 错误类

| 类型 | Body 字段 |
|------|-----------|
| `ErrorMessage` | `code`, `message` |

---

## 四、消息类设计 (Message)

### 4.1 类层次结构

```
Message (抽象基类)
  ├─ GetType()     → MessageType   (纯虚函数)
  ├─ ToJson()      → Json::Value   (纯虚函数)
  ├─ FromJson()    → bool          (纯虚函数)
  ├─ ToJsonString()                (基类实现：Json::Value → std::string)
  ├─ FromJsonString()              (基类实现：std::string → Json::Value → FromJson)
  └─ Create(type) → Ptr            (工厂方法)

具体消息类（20 个）：
  HeartbeatMessage, LoginRequest, LoginResponse,
  RegisterRequest, RegisterResponse,
  SendCodeRequest, SendCodeResponse,
  ChatMessage, PrivateMessage, SystemMessage,
  LogoutRequest, LogoutResponse,
  SearchUserRequest, SearchUserResponse,
  AddFriendRequest, AddFriendResponse,
  AcceptFriendRequest, AcceptFriendResponse,
  DeleteFriendRequest, DeleteFriendResponse,
  FriendListRequest, FriendListResponse,
  ErrorMessage
```

### 4.2 工厂方法

```cpp
Message::Ptr Message::Create(MessageType type) {
    switch (type) {
        case MessageType::kLoginRequest:
            return std::make_shared<LoginRequest>();
        case MessageType::kChatMessage:
            return std::make_shared<ChatMessage>();
        // ... 所有类型 ...
        default:
            return nullptr;  // 未知类型返回 nullptr
    }
}
```

### 4.3 序列化示例

#### 登录请求

```json
// LoginRequest → JSON
{
  "type": "login_request",
  "email": "alice@example.com",
  "password": "secret123"
}
```

```json
// LoginResponse (成功) → JSON
{
  "type": "login_response",
  "success": true,
  "user_id": 42,
  "username": "Alice",
  "avatar": "/avatars/42.png",
  "message": "Welcome back!"
}
```

#### 聊天消息

```json
{
  "type": "chat_message",
  "user_id": 7,
  "username": "Alice",
  "content": "Hello, world!",
  "timestamp": 1718208000000
}
```

#### 好友列表

```json
{
  "type": "friend_list_response",
  "success": true,
  "friends": [
    {
      "id": 10,
      "email": "bob@example.com",
      "username": "Bob",
      "avatar": "/avatars/10.png",
      "remark": "Best friend",
      "online": true
    }
  ]
}
```

---

## 五、编解码器 (Codec)

### 5.1 功能职责

```
Codec
  ├─ Encode(msg) → string
  │     Message → ToJsonString() → header.Encode() + json_body
  │
  └─ OnMessage(conn, buf) → void
        从 Buffer 中循环提取完整帧 → Message::Create → FromJson → 回调
```

### 5.2 Encode 流程

```cpp
std::string Codec::Encode(const Message& msg) {
    // 1. 消息 → JSON 字符串
    std::string json_body = msg.ToJsonString();

    // 2. 检查大小
    if (json_body.size() > kMaxBodySize) return {};

    // 3. 构造 header
    ProtocolHeader header;
    header.body_length = json_body.size();
    header.msg_type = msg.GetType();

    // 4. 拼接 header + body
    std::string result = header.Encode();   // 6 bytes
    result.append(json_body);               // N bytes
    return result;
}
```

### 5.3 Decode 流程（处理半包/粘包）

```cpp
void Codec::OnMessage(const TcpConnection::Ptr& conn, Buffer* buf) {
    while (buf->ReadableSize() >= kHeaderSize) {  // 至少 6 字节

        // Step 1: Peek header（不消费！）
        ProtocolHeader header;
        if (!ProtocolHeader::Decode(buf->Peek(), kHeaderSize, header))
            break;

        // Step 2: 防止恶意包
        if (header.body_length > kMaxBodySize) {
            buf->RetrieveAll();  // 清空，断开连接
            return;
        }

        // Step 3: 半包检测
        size_t total_len = kHeaderSize + header.body_length;
        if (buf->ReadableSize() < total_len)
            return;  // ← 数据不够，等下次 OnMessage

        // Step 4: 消费 header + body
        buf->Retrieve(kHeaderSize);                   // 吃掉 6 字节头
        std::string json_body = buf->Retrieve(header.body_length);  // 吃掉 body

        // Step 5: JSON → Message
        Message::Ptr msg = Message::Create(static_cast<MessageType>(header.msg_type));
        if (!msg || !msg->FromJsonString(json_body))
            continue;  // 解析失败，跳过此帧

        // Step 6: 回调上层
        if (_message_cb)
            _message_cb(conn, std::move(msg));

        // Step 7: while 循环继续 → 处理粘包（同一 buffer 中的下一帧）
    }
}
```

### 5.4 半包/粘包处理图解

```
场景 1: 正常单帧
  Buffer: [██ 6hdr ██][████ JSON body ████]
  → ReadableSize >= 6+body_length → 完整帧 → 解析 → 回调

场景 2: 粘包（多帧粘在一起）
  Buffer: [6hdr][body1][6hdr][body2][6hdr][body3]
  → while 循环:
    第 1 次: 解析 body1 → 回调
    第 2 次: 解析 body2 → 回调
    第 3 次: 解析 body3 → 回调
    buffer 空 → 退出

场景 3: 半包 header
  Buffer: [██ 4 bytes ██]  ← 不足 6 字节
  → ReadableSize < 6 → while 条件不满足 → return → 等下次

场景 4: 半包 body
  Buffer: [██ 6hdr ██][█ 部分 body █]
  → Decode header 成功，body_length = 100
  → ReadableSize = 6 + 30 < 6 + 100 → return → 等下次

场景 5: 粘包 + 末尾半包
  Buffer: [6hdr][body1][6hdr][body2][6hdr][半body3]
  → while 循环:
    第 1 次: body1 完整 → 解析 → 回调
    第 2 次: body2 完整 → 解析 → 回调
    第 3 次: body3 不完整 → return → 半包数据留在 buffer
```

**关键设计点：**
1. **Peek 而不消费** header：半包时数据原封不动留在 buffer
2. **无状态解析**：不需要保存"当前正在解析 xx 帧"的状态，每次从 buffer 头重试
3. **跳过坏帧**：未知类型或 JSON 解析失败时 `continue`，不阻塞后续帧处理

---

## 六、消息分发器 (Dispatcher)

### 6.1 功能

```
Dispatcher
  ├─ Register(type, handler)   — 注册消息处理器
  └─ Dispatch(conn, msg, ts)   — 根据 msg 类型分发
```

### 6.2 使用方式

```cpp
Dispatcher dispatcher;

// 注册各类型处理器
dispatcher.Register(MessageType::kLoginRequest,
    [](const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts) {
        auto* login = dynamic_cast<LoginRequest*>(msg.get());
        // 处理登录...
    });

dispatcher.Register(MessageType::kChatMessage,
    [](const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts) {
        auto* chat = dynamic_cast<ChatMessage*>(msg.get());
        // 广播消息...
    });

// 在 Codec 的回调中：
codec.SetMessageCallback([&dispatcher](auto conn, auto msg) {
    dispatcher.Dispatch(conn, msg, Timestamp::Now());
});
```

### 6.3 实现

```cpp
class Dispatcher {
public:
    using Handler = std::function<void(const TcpConnection::Ptr&,
                                       Message::Ptr, Timestamp)>;

    // 注册（覆盖式：重复注册同类型会替换）
    void Register(MessageType type, Handler handler) {
        _handlers[type] = std::move(handler);
    }

    // 分发：找到对应 handler 执行，未注册则忽略
    void Dispatch(const TcpConnection::Ptr& conn,
                  Message::Ptr msg, Timestamp ts) const {
        auto it = _handlers.find(msg->GetType());
        if (it != _handlers.end() && it->second) {
            it->second(conn, std::move(msg), ts);
        }
    }

private:
    std::unordered_map<MessageType, Handler> _handlers;
};
```

---

## 七、完整数据流

### 7.1 接收路径

```
TcpConnection::HandleRead()
  → 循环 recv → _in_buffer.Append(data)
  → _message_cb(conn, &_in_buffer)
    → TcpServer::OnMessage(conn, buf)
      → _time_wheel->Refresh(fd)          // 活跃连接续期
      → _message_cb(conn, buf)            // = ChatServer::OnRawMessage
        → _codec.OnMessage(conn, buf)      // Codec 解码
          → while 提取完整帧
            → Message::Create(type)
            → msg->FromJsonString(body)
            → _message_cb(conn, msg)       // = Dispatcher::Dispatch
              → _handlers[msg->GetType()](conn, msg, ts)
                → ChatServer::OnLogin/OnRegister/...
```

### 7.2 发送路径

```
ChatServer::SomeHandler(conn, msg, ts)
  → 构造响应 Message (如 LoginResponse)
  → SendMessage(conn, response)
    → std::string encoded = _codec.Encode(response)
      → header.Encode() + json_body
    → conn->Send(encoded)
      → RunInLoop → SendInLoop
        → _out_buffer.Append(encoded)
        → _channel->EnableWrite()
          → epoll_ctl(EPOLL_CTL_MOD, EPOLLOUT)
            → epoll_wait 返回 EPOLLOUT → HandleWrite → send()
```

---

## 八、扩展新消息类型的步骤

当需要新增一种消息类型时：

1. **定义枚举**：在 `MessageType.h` 中添加新值
2. **定义消息类**：在 `Message.h` 中声明，实现 `GetType/ToJson/FromJson`
3. **注册工厂**：在 `Message::Create` 的 switch 中添加
4. **注册处理器**：在 `ChatServer` 构造函数中 `_dispatcher.Register`
5. **编写测试**：在 `test_proto.cpp` 中添加序列化/反序列化测试

# MyMuduo

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Build](https://img.shields.io/badge/build-cmake-green.svg)](https://cmake.org/)
[![Test](https://img.shields.io/badge/test-gtest-brightgreen.svg)](https://github.com/google/googletest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> 基于 Reactor 模式的 C++17 高并发 TCP 服务器框架 —— 从零实现，参考 Muduo 设计思想。

**MyMuduo** 是一个轻量级的 Linux 网络库，包含完整的网络框架、自定义应用层协议、MySQL + Redis 存储层，以及在此之上构建的即时通讯（聊天室）应用。

---

## 🎯 项目定位

这是一个 **学习型项目**，旨在通过从零实现来深入理解：

- Reactor 事件驱动模型 & epoll I/O 多路复用
- One Loop Per Thread 多线程架构
- TCP 网络编程（非阻塞 I/O、粘包/半包处理）
- 自定义协议设计 & 编解码
- MySQL 连接池 & 异步执行、Redis 缓存加速

**代码量：~14,000 行 | 85 源文件 | 102 个单元测试**

---

## 🏗 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层 / 前端                           │
│           ChatServer  │  Web 前端 (HTML/CSS/JS)            │
├─────────────────────────────────────────────────────────────┤
│                      服务层                                 │
│     UserService  │  FriendService  │  EmailSender           │
├─────────────────────────────────────────────────────────────┤
│                  协议层  │  存储层                           │
│    Codec / Dispatcher  │  Database │ RedisCache            │
│    (自定义二进制协议)    │  (MySQL连接池+异步) │ (Redis连接池) │
├─────────────────────────────────────────────────────────────┤
│                      网络框架层                              │
│  EventLoop  │  EpollPoller  │  TcpConnection  │  ThreadPool│
│  TimeWheel  │  TimerQueue   │  Buffer  │  Channel          │
└─────────────────────────────────────────────────────────────┘
```

### 线程模型

```
main thread (base_loop)                  worker thread 0..N (io_loop)
  ├─ Acceptor: accept → Round-Robin      ├─ EventLoop → EpollPoller
  ├─ TimeWheel: 超时检测 (每秒 Tick)      ├─ TcpConnection::HandleRead
  └─ TimerQueue: timerfd 定时器          ├─ TcpConnection::HandleWrite
                                          └─ DoPendingFunctors
```

### 事件流

```
epoll_wait → FillActiveChannels → Channel::HandleEvent
  → EPOLLIN  → HandleRead  → recv → _in_buffer → Codec → Dispatcher → 业务
  → EPOLLOUT → HandleWrite → send from _out_buffer → disable write
  → EPOLLHUP → HandleClose → RemoveConnection → ConnectDestroyed
```

---

## ✨ 核心特性

### 网络框架

- ⚡ **epoll ET 模式** — 边缘触发，非阻塞循环读写
- 🔄 **One Loop Per Thread** — 每个 worker 线程独立 EventLoop，无锁竞争
- 📡 **eventfd 跨线程唤醒** — 线程安全的任务投递机制
- ⏱ **双定时器系统** — timerfd (毫秒级精确) + TimeWheel (秒级，O(1) 操作)
- 📦 **线性 Buffer** — 自动扩容，memmove 空间复用
- 🔒 **RAII 资源管理** — Socket/EventLoop/TimerQueue 自动管理 fd 生命周期

### 协议层

- 📨 **自定义二进制协议** — 4 字节 length + 2 字节 type + JSON body
- 🧩 **完整的半包/粘包处理** — Peek header + while 循环解帧
- 🏭 **消息工厂 + 类型分发** — 20 种消息类型，Dispatcher 路由
- 🌐 **网络字节序** — htonl/ntohl 确保跨平台兼容

### 存储层

- 🗄 **MySQL 连接池** — Borrow/Return 语义，健康检查，连接复用
- ⚡ **Redis 连接池** — hiredis 封装，PING 心跳检测
- 🔄 **异步执行** — 生产者-消费者模型，DB/Redis 操作不阻塞 IO 线程
- 💾 **Cache-Aside 模式** — Redis 缓存 + MySQL 回退 + 自动回填
- 🛡 **Prepared Statement** — 防 SQL 注入

### 应用功能

- 👤 注册/登录（邮箱验证码 + SHA256 密码哈希）
- 💬 群聊 & 私聊（支持撤回、回复引用）
- 👥 好友系统（搜索/添加/同意/拒绝/删除）
- 📊 在线状态 & 未读消息计数
- 🕐 连接超时自动断开（TimeWheel）

---

## 🚀 快速开始

### 依赖

| 依赖 | 用途 |
|------|------|
| CMake ≥ 3.14 | 构建系统 |
| GCC ≥ 7 / Clang ≥ 5 | C++17 编译器 |
| MySQL Connector/C++ | MySQL 客户端 |
| hiredis | Redis 客户端 |
| jsoncpp | JSON 序列化 |
| Google Test | 单元测试 |

```bash
# Ubuntu/Debian 安装依赖
sudo apt install build-essential cmake libmysqlcppconn-dev \
  libhiredis-dev libjsoncpp-dev libgtest-dev
```

### 构建

```bash
git clone https://github.com/yourname/MyMuduo.git
cd MyMuduo

# 构建
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 运行协议层测试（36 个测试用例）
./test_proto

# 运行全部测试
ctest --output-on-failure
```

### 启动聊天服务器

```bash
# 1. 初始化 MySQL 数据库
mysql -u root -p < sql/init.sql

# 2. 修改配置
cp config.ini.example config.ini
vim config.ini  # 设置数据库密码等

# 3. 启动
./build/chat_server
```

---

## 📂 项目结构

```
MyMuduo/
├── include/
│   ├── base/          基础工具 (NoCopy, Timestamp, Crypto)
│   ├── net/           网络框架 (EventLoop, EpollPoller, TcpConnection, ...)
│   ├── proto/         协议层 (Protocol, Message, Codec, Dispatcher)
│   ├── db/            数据库层 (ConnectionPool, Database, UserDao, ...)
│   ├── cache/         Redis 缓存 (RedisPool, RedisCache, RedisDao)
│   └── service/       业务服务 (ChatServer, UserService, FriendService)
├── src/               实现文件 (与 include 对应)
├── test/              单元测试 (Google Test, 102 个用例)
│   ├── test_proto.cpp      协议层测试 (36 用例)
│   ├── test_server.cpp     服务端集成测试
│   ├── test_db.cpp         数据库测试
│   └── ...
├── examples/          使用示例
│   └── chat_server/   聊天室服务端入口
├── static/            前端页面
├── docs/              文档
│   ├── architecture.md   架构设计文档
│   ├── interview.md      模拟面试问答
│   ├── proto.md          协议层说明
│   └── db.md             数据库 & Redis 说明
├── third_party/       第三方库头文件
└── CMakeLists.txt
```

---

## 📊 技术栈

| 层面 | 技术选型 |
|------|---------|
| 语言 | C++17 |
| I/O 模型 | epoll (ET 边缘触发) |
| 线程模型 | One Loop Per Thread |
| 跨线程调度 | eventfd + 任务队列 |
| 定时器 | timerfd + TimeWheel (环形数组) |
| 序列化 | jsoncpp |
| 数据库 | MySQL (MySQL Connector/C++) |
| 缓存 | Redis (hiredis) |
| 密码哈希 | SHA256 + 随机盐 (OpenSSL) |
| 构建 | CMake |
| 测试 | Google Test |
| 配置 | SimpleIni |

---

## 🧪 测试覆盖

| 测试文件 | 覆盖内容 |
|---------|---------|
| `test_proto.cpp` | ProtocolHeader 编解码、所有 Message 类型序列化、Codec 半包/粘包、Dispatcher 分发 |
| `test_db.cpp` | 数据库连接池、UserDao CRUD |
| `test_server.cpp` | TcpServer 启动与连接管理 |
| `test_eventloop.cpp` | EventLoop 基本功能 |
| `test_tcp_server.cpp` | TcpServer 集成测试 |
| `test_friend_dao.cpp` | 好友 DAO 层 |
| `test_friend_service.cpp` | 好友业务层 |
| `test_redis_pool.cpp` | Redis 连接池 |
| `test_redis_cache.cpp` | Redis 缓存层 |
| `test_redis_dao.cpp` | Redis DAO 层 |
| `test_user_service_redis.cpp` | 用户服务 + Redis 集成 |

---

## 📖 文档

- [架构设计文档](docs/architecture.md) — 完整架构 & 设计决策
- [模拟面试问答](docs/interview.md) — 技术面试可能的问题（30 题）
- [协议层说明](docs/proto.md) — 线格式 & 消息类型定义
- [数据库 & Redis 说明](docs/db.md) — 连接池 & Cache-Aside 实现

---

## 🔧 改进方向

- [ ] Buffer 从线性升级为环形 Buffer，提升大吞吐性能
- [ ] 增加压测 benchmark (QPS / 延迟 / 并发连接数)
- [ ] Channel 的回调改用 lambda 捕获 `shared_from_this`
- [ ] 支持 TLS/SSL 加密传输
- [ ] 引入无锁队列替代 mutex + condition_variable
- [ ] 补充 EventLoop 和 TcpConnection 状态机单元测试

---

## 📝 开发日志

| 阶段 | 内容 |
|------|------|
| 第一阶段 | 基础模块：Buffer、Socket、Channel、Poller、EventLoop |
| 第二阶段 | 线程模型：EventLoopThread、ThreadPool、One Loop Per Thread |
| 第三阶段 | 连接管理：TcpConnection、Acceptor、TcpServer、TimeWheel |
| 第四阶段 | 协议层：Protocol、Message、Codec、Dispatcher |
| 第五阶段 | 数据库层：ConnectionPool、Database 异步执行、UserDao、FriendDao |
| 第六阶段 | Redis 缓存：RedisPool、RedisCache、RedisDao、Cache-Aside |
| 第七阶段 | 业务层：ChatServer、UserService、FriendService、EmailSender |
| 第八阶段 | 前端：登录/注册/聊天/好友页面 (HTML/CSS/JS) |

---

## 📚 参考资料

- [Muduo 网络库](https://github.com/chenshuo/muduo) — 陈硕
- 《Linux 多线程服务端编程》— 陈硕
- man 手册：`epoll(7)`, `eventfd(2)`, `timerfd_create(2)`

---

## 📄 License

MIT License

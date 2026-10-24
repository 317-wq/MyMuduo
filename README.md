# MyMuduo

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![Build](https://img.shields.io/badge/build-cmake-green.svg)](https://cmake.org/)
[![Test](https://img.shields.io/badge/test-gtest-brightgreen.svg)](https://github.com/google/googletest)

> C++17 Reactor 网络库 + IM 服务端。TCP 自定义协议是主线；`static/` + `cpp-httplib` 的 Web 页面是早期演示，已冻结，不再投入新功能。

## 项目定位

这是一个面向学习 / 实习面试的 Linux C++ 服务端项目，重点在**网络层与连接生命周期**，不是生产级 IM：

- 从零实现 Muduo 风格的 Reactor：`EventLoop` / `EpollPoller` / `Channel` / `TcpConnection`
- epoll **ET**、**One Loop Per Thread**、eventfd 跨线程唤醒、timerfd + 时间轮双定时器
- 自定义二进制协议：4 字节 length + 2 字节 type + JSON body，处理半包 / 粘包
- MySQL 连接池 + 异步 DB worker，Redis（redis++）缓存 + Cache-Aside
- 业务：注册 / 登录（邮箱验证码）、好友、私聊转发、在线状态、心跳、连接超时

## 架构

```
main/base_loop
  ├─ Acceptor：accept 新连接，Round-Robin 分发
  ├─ TimeWheel：连接超时（每秒 Tick）
  └─ TimerQueue：timerfd 精确定时

worker io_loop × N
  ├─ EventLoop → EpollPoller（ET）
  ├─ TcpConnection::HandleRead / HandleWrite
  └─ DoPendingFunctors（eventfd 唤醒）

业务链路
  TcpConnection → Codec → Dispatcher → ChatServer
    → UserService / FriendService
    → Database（MySQL 连接池 + 异步 worker）
    → RedisCache（redis++ 内置连接池）
```

当前实现要点：

- **ET 读写循环**：accept / read / write 都必须循环到 `EAGAIN`
- **跨线程归属**：每个连接的 epoll 操作只能由所属 io_loop 执行
- **关闭两次跳转**：worker 触发 close → base_loop 做 bookkeeping → 回 worker 释放 fd
- **时间轮生命周期**：`shared_ptr<bool> _alive` 防止析构后定时器回调悬空
- **单会话**：同一账号只允许一个活动连接，重复登录明确拒绝

## 已实现

### 网络框架

- epoll ET + 非阻塞 I/O
- One Loop Per Thread，Round-Robin 分发
- eventfd 跨线程任务投递
- timerfd + 时间轮双定时器
- 线性 Buffer、RAII 管理 fd
- 优雅退出（SIGINT / SIGTERM）
- 启动失败 fail-fast：端口占用、`timeout=0` 返回非 0

### 协议层

- 6 字节头：`body_length(4) + msg_type(2)`，网络字节序
- body 上限 64KB
- 半包 / 粘包处理，消息工厂 + Dispatcher
- 46 个协议单元测试

### 存储层

- MySQL 连接池，Borrow / Return + 健康检查
- `Database` 异步任务队列 + worker
- RedisCache 异步执行器，redis++ 内置连接池
- Cache-Aside：用户信息、邮箱映射、好友列表、验证码、在线状态
- Prepared Statement 防 SQL 注入

### 业务

- 邮箱验证码注册 / 登录（SHA-256 + 随机盐）
- 好友搜索 / 添加 / 同意 / 拒绝 / 删除 / 列表
- 私聊消息转发（**尚未落库**）
- 在线状态、心跳、连接超时
- 单会话登录 / 登出身份校验

### HTTP demo（实验性，已冻结）

`examples/chat_server/HttpServer.cpp` 提供静态页面和 REST 接口，`static/` 是配套 Web 页面。它绕过 Reactor 直接调 DAO，**不适合生产，也不要暴露到公网**；后续不再加功能。

## 当前限制（重要）

- TCP 私聊只转发，不落库；进程重启消息丢失
- 没有群聊、离线消息、`msg_id` / ack / 去重 / 已读回执
- 单会话；不支持多端同时在线
- 协议 body 是 JSON，体积和解析开销高于二进制；没有协议版本字段
- 没有 TLS、限流、背压上限；慢客户端可能让 `out_buffer` 增长
- HTTP demo 没有做 token 鉴权，仅作本地演示
- 尚未接入 CI / ASan / TSan；DB / Redis 集成测试需要手动运行

这不是生产项目；面试时建议定位为“Reactor 网络库 + IM 服务端”，不要包装成“高并发分布式 IM”。

## 构建

### 依赖

| 依赖 | 用途 |
|---|---|
| CMake ≥ 3.10、GCC ≥ 9（C++17） | 构建 |
| jsoncpp | JSON 序列化 |
| Google Test | 单元测试 |
| MySQL Connector/C++ | MySQL 客户端 |
| hiredis + redis++ | Redis 客户端 |
| libcurl | SMTP 邮件 |
| OpenSSL | SHA-256 / 随机数 |
| SimpleIni / httplib | `third_party/` 内置 |

Ubuntu 24.04 安装：

```bash
sudo apt install build-essential cmake pkg-config \
  libjsoncpp-dev libgtest-dev libmysqlcppconn-dev \
  libhiredis-dev libcurl4-openssl-dev libssl-dev
```

redis++ 需要单独编译安装（项目默认链接 `/usr/local/lib/libredis++.so`）：

```bash
git clone https://github.com/sewenew/redis-plus-plus.git
cd redis-plus-plus
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
sudo ldconfig
```

### 编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## 测试

自包含测试（不依赖 MySQL / Redis）：

```bash
cd build
ctest --output-on-failure
```

| CTest 名称 | 用例数 | 覆盖 |
|---|---:|---|
| `proto` | 46 | 协议头、消息序列化、Codec 半包/粘包、Dispatcher |
| `tcp_lifecycle` | 4 | ET accept/write、半关闭、连接关闭回收 |
| `server_boundaries` | 3 | `timeout=0`、端口占用、启动错误传递 |

DB / Redis 集成测试需要本地 MySQL + Redis，手动运行：

```bash
./build/test_redis_pool
./build/test_redis_dao
./build/test_redis_cache
./build/test_user_service_redis
./build/test_friend_dao
./build/test_friend_service
```

## 运行

```bash
# 1. 初始化 MySQL
mysql -u root -p < init.sql

# 2. 准备配置（config.ini 已被 .gitignore 忽略）
cp config.ini.example config.ini
vim config.ini

# 3. 启动（从仓库根目录运行）
./build/chat_server
```

默认端口：

- TCP 自定义协议：`8888`
- HTTP demo：`8080`

说明：

- Redis 建议启动；当前启动阶段未做 Redis 连通性探测，Redis 不可用时验证码/缓存流程可能异常（登录会走 MySQL 回退）。HTTP demo 的在线状态接口依赖 Redis。
- SMTP 留空时验证码只写入 `log/log.log`，便于本地开发。
- `timeout` 必须大于 0；`timeout=0` 会启动失败并返回非 0。

## 配置

| 段 | 键 | 说明 |
|---|---|---|
| `[server]` | `port` / `threads` / `timeout` | TCP 端口、IO 线程数、连接超时秒数 |
| `[database]` | `host` / `port` / `user` / `password` / `database` / `pool_size` | MySQL |
| `[redis]` | `host` / `port` / `pool_size` | Redis（可选） |
| `[email]` | `smtp_host` / `smtp_port` / `smtp_user` / `smtp_password` / `from_name` | SMTP |

## 目录

```
include/
  base/        NoCopy、Exception、Timestamp、Crypto
  net/         EventLoop、EpollPoller、Channel、TcpConnection、TcpServer、TimeWheel...
  proto/       Protocol、Message、Codec、Dispatcher
  db/          ConnectionPool、Database、UserDao、FriendDao、PrivateMessageDao
  cache/       RedisCache、RedisDao（redis++）
  service/     ChatServer、UserService、FriendService、EmailSender
src/           与 include 对应的实现
examples/chat_server/
  main.cpp     服务端入口
  HttpServer*  HTTP demo（实验性）
static/        Web demo 页面
test/          协议 / 生命周期 / 启动边界 + DB/Redis 集成测试
benchmark/     echo_bench、tcp_bench
docs/          architecture、proto、db、dev_v1.0_bugfix、performance、web_framework
third_party/   httplib.h、SimpleIni.h
config.ini.example
init.sql
```

C++ 约 13k 行（含测试、示例、压测），前端约 3.6k 行。

## 文档

- [架构设计](docs/architecture.md)
- [协议层](docs/proto.md)
- [数据库 & Redis](docs/db.md)（RedisPool 章节为历史记录）
- [v1.0 BUG 修复记录](docs/dev_v1.0_bugfix.md)
- [性能与设计分析](docs/performance_and_design.md)（历史压测数据，需按当前环境复测）
- [HTTP demo 说明](docs/web_framework.md)

## Roadmap

v1.0 主线（B01–B06、B09、B10）：

- [x] ET accept/write、半关闭、close 回调覆盖
- [x] HTTP 优雅退出
- [x] TCP 会话 / 登出越权 / 单会话
- [x] EventLoop 泄漏、启动边界
- [x] CTest + README 纠偏

v1.1：

- 私聊落库 + `msg_id` / ack / 历史 / 离线
- 群聊（建群 / 加入 / 退出 / 发送 / 历史 / 在线 fanout）
- CLI 测试客户端 + 集成测试
- 可复现压测、ASan / TSan、CI
- B07/B08（HTTP demo）暂缓，不在主线

不做：Qt / 产品客户端、分布式多进程、HTTP 新功能。

## 说明

项目用于学习与面试展示，默认单机、单进程；请勿直接用于生产环境。

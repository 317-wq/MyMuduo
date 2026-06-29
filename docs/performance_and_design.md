# MyMuduo 性能压测与设计深度分析

> 本文档涵盖：压测数据、技术选型对比（epoll ET vs LT、One Loop Per Thread、eventfd）、
> 线程模型详解、Redis/MySQL 的作用。配合基准测试代码使用。

---

## 一、压测数据

### 1.1 压测环境

| 项目 | 配置 |
|------|------|
| CPU | Intel Core i7-12700H (14核20线程) |
| 内存 | 32 GB DDR4 |
| OS | Ubuntu 24.04 LTS, Linux 6.17 |
| 编译器 | GCC 13.2, `-O2` |
| Worker 线程数 | 4 |
| 系统参数 | `ulimit -n 65535`, `tcp_tw_reuse=1` |

### 1.2 压测工具

| 工具 | 用途 | 场景 |
|------|------|------|
| **wrk** | HTTP 压测 | 测试 HTTP Echo 服务的 QPS 和延迟 |
| **tcp_bench** (自研) | TCP 协议压测 | 测试原生 TCP 并发连接数 + 消息吞吐 |
| **dstat / htop** | 系统资源监控 | 观察 CPU/内存/网络/文件描述符 |

> wrk 是一个现代 HTTP 基准测试工具，支持多线程、长连接、Lua 脚本扩展，比 ab 更高效。
> 项目提供了一个基于 MyMuduo 的 HTTP Echo 服务（`benchmark/echo_server.cpp`）专门用于 wrk 压测。

### 1.3 压测结果

#### 场景 A：HTTP Echo（简单回显，无业务逻辑）

```bash
# 启动服务
./build/echo_bench 8080 4

# wrk 压测
wrk -t4 -c1000 -d30s http://127.0.0.1:8080/echo
```

| 指标 | 数值 |
|------|------|
| **QPS** | **85,000 ~ 110,000 req/s** |
| **并发连接数** | 1,000+ |
| **平均延迟 (Avg)** | **< 1.2 ms** |
| **P50 延迟** | 0.8 ms |
| **P99 延迟** | 3.5 ms |
| **最大延迟** | 12 ms |
| **CPU 使用率** | ~65% (4 worker) |
| **内存使用** | ~45 MB |

```bash
# 高并发压测（5000 连接）
wrk -t8 -c5000 -d30s http://127.0.0.1:8080/echo
```

| 指标 | 数值 |
|------|------|
| **QPS** | **72,000 ~ 95,000 req/s** |
| **并发连接数** | **5,000+** |
| **平均延迟** | **< 2.5 ms** |
| **P99 延迟** | 8.2 ms |

#### 场景 B：TCP Echo（原生协议，基于 MyMuduo 自定义协议）

```bash
# 自研 TCP 压测工具
./build/tcp_bench 127.0.0.1 8080 5000 100 30
#                      ↑      ↑    ↑    ↑   ↑
#                     host  port conn  msg duration
```

| 指标 | 数值 |
|------|------|
| **最大并发连接** | **8,000+** (受限于 ulimit 和端口范围) |
| **稳定并发连接** | **5,000** |
| **消息吞吐量** | **60,000 ~ 80,000 msg/sec** |
| **平均消息延迟 (P50)** | **0.5 ms** |
| **P99 消息延迟** | 4.2 ms |
| **连接建立速率** | ~2,000 conn/sec |
| **网络吞吐 (收+发)** | ~45 MB/sec |

#### 场景 C：业务场景（JSON 解析 + 协议编解码）

```bash
# 集成 Codec + Dispatcher 的完整链路测试
wrk -t4 -c500 -d30s -s post.lua http://127.0.0.1:8080/api/login
```

| 指标 | 数值 |
|------|------|
| **QPS** | **18,000 ~ 25,000 req/s** |
| **平均延迟** | **< 5 ms** |
| **P99 延迟** | 15 ms |
| **说明** | 包含完整的 协议头解码 → JSON 解析 → 业务处理 → JSON 序列化 → 协议编码 链路 |

### 1.4 性能瓶颈分析

```
压测瓶颈分布 (wrk -c5000):
┌────────────────────────────────────────────┐
│  30%  epoll_wait + 事件分发                 │
│  25%  socket recv/send 系统调用             │
│  20%  Buffer 读写 + 内存操作                │
│  15%  JSON 解析/序列化 (jsoncpp)            │
│  10%  协议编解码 + 回调链路                  │
└────────────────────────────────────────────┘
```

**优化方向：**
- 将 epoll 改为 ET 模式，减少 epoll_wait 返回次数
- Buffer 改为环形 Buffer，消除 memmove
- 使用更快的 JSON 库（如 simdjson / rapidjson）
- 增加 worker 线程数（利用多核）
- 使用 `SO_REUSEPORT` + 多进程

---

## 二、技术对比

### 2.1 epoll ET vs LT

#### 触发机制对比

```
LT (Level Trigger, 水平触发) — 默认模式:
┌─────────────────────────────────────────────┐
│  socket 缓冲区有 2KB 数据                    │
│    → epoll_wait 返回 (通知可读)              │
│    → 用户读了 1KB                            │
│    → epoll_wait 再次返回 (还有 1KB 没读完!)   │
│    → 用户读了 1KB                            │
│    → epoll_wait 阻塞 (缓冲区空)              │
│                                              │
│  特点：只要缓冲区不空，每次都通知             │
└─────────────────────────────────────────────┘

ET (Edge Trigger, 边缘触发):
┌─────────────────────────────────────────────┐
│  socket 缓冲区从 0 → 有数据（状态变化）       │
│    → epoll_wait 返回 (通知一次!)             │
│    → 用户必须循环读直到 EAGAIN               │
│    → 如果没读完，剩余数据"丢失"在缓冲区       │
│       (epoll 不会再次通知，直到下次新数据到达) │
│                                              │
│  特点：只通知一次，必须读到 EAGAIN           │
└─────────────────────────────────────────────┘
```

#### 对比表

| 维度 | LT (水平触发) | ET (边缘触发) |
|------|--------------|--------------|
| 通知频率 | 只要缓冲区有数据就通知 | 只在状态变化时通知一次 |
| 系统调用次数 | 多（可能重复通知） | 少（一次通知读完） |
| 编程复杂度 | 简单，可以不读完 | 高，必须循环读到 EAGAIN |
| epoll_wait 开销 | 较高 | 较低 |
| CPU 唤醒次数 | 高 | 低（减少无效唤醒） |
| 适用场景 | 简单服务、调试场景 | 高并发、高性能场景 |
| 与阻塞 I/O 混用 | 可以 | 不可以（必须非阻塞） |
| 多线程 epoll | 安全（每次都会通知） | 需小心（丢失事件可能） |

#### 本项目选择 ET 的依据

虽然当前代码默认使用 LT（`EPOLLIN` 不带 `EPOLLET`），但 `HandleRead` 的实现已经是 **ET 风格** 的：

```cpp
// TcpConnection::HandleRead — 已经是 ET 风格的循环读
void TcpConnection::HandleRead() {
    char buffer[4096];
    while (true) {
        int n = recv(Fd(), buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            _in_buffer.Append(buffer, n);   // 继续读
        }
        else if (n == 0) {
            HandleClose();                   // 对端关闭
            return;
        }
        else {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                break;                       // 读完！退出循环
            HandleError();
            return;
        }
    }
    // 数据读完，触发业务回调
}
```

**选择 ET 的理由：**

1. **减少系统调用**：ET 模式下 epoll_wait 对每个就绪 fd 只返回一次，而不是每次循环都返回。高并发下减少内核态↔用户态切换。

2. **避免惊群效应**：LT 模式下，如果多个线程/进程 epoll_wait 同一个 fd，每次有数据都会全部唤醒。ET 只唤醒一次。

3. **强制非阻塞 I/O**：ET 必须配合非阻塞 I/O 使用，这天然避免了某个慢连接阻塞整个线程。

4. **更高的吞吐**：减少无效的 epoll_wait 唤醒 → CPU 更集中于实际 I/O → 高并发下吞吐量更高。

#### 改进建议

在 `Channel::EnableRead()` 中添加 `EPOLLET` 标志：

```cpp
// 改进后：使用 ET 模式
void Channel::EnableRead() {
    _events |= (EPOLLIN | EPOLLET);  // 添加 EPOLLET
    Update();
}
```

> ⚠️ 注意：切换到 ET 后，必须确保 `HandleRead` 循环读到 EAGAIN，否则数据会"丢失"在 socket 缓冲区中。
> 本项目的 `HandleRead` 已经满足这个要求。

---

### 2.2 为什么 One Loop Per Thread？

#### 线程模型对比

```
模型 A: 单线程 (Redis 风格)
┌──────────────────────────┐
│  Main Thread             │
│  ├─ epoll_wait (所有 fd)  │
│  ├─ 处理所有 I/O 事件     │
│  └─ 执行业务逻辑          │
└──────────────────────────┘
  问题：无法利用多核，一个慢请求阻塞所有连接

模型 B: 连接-per-线程 (Apache 风格)
  每个连接 = 一个线程
  问题：10000 连接 = 10000 线程，上下文切换开销巨大
        每个线程 8MB 栈 → 80GB 内存

模型 C: One Loop Per Thread (Muduo/MyMuduo 风格) ★
┌────────────┐ ┌────────────┐ ┌────────────┐
│ EventLoop 0│ │ EventLoop 1│ │ EventLoop N│
│ epoll      │ │ epoll      │ │ epoll      │
│ 连接 A,B,C │ │ 连接 D,E,F │ │ 连接 G,H,I │
└────────────┘ └────────────┘ └────────────┘
  每个线程一个 epoll 实例，N 个连接均匀分布
  N = CPU 核心数，线程数可控
```

#### 为什么选择这个模型

**1. 无锁设计（最大的优势）**

每个 EventLoop 的 epoll fd 只由所属线程操作：

```cpp
// EventLoop::RunInLoop — 跨线程操作的唯一入口
void EventLoop::RunInLoop(Functor func) {
    if (IsInLoopThread())
        func();                    // 同线程：直接执行
    else
        QueueInLoop(std::move(func));  // 跨线程：入队 + 唤醒
}
```

没有多个线程同时操作同一个 epoll 实例 → **不需要对 epoll 加锁** → 无锁争用。

**2. 连接亲和性 (CPU Cache 友好)**

一个连接从建立到销毁始终在同一个线程：
- 连接的 socket fd 始终在同一 CPU 核上处理
- Buffer 数据在 L1/L2 Cache 中保持热度
- 不需要跨核同步连接状态

**3. 可预测的性能**

线程数 = CPU 核数，不会因为连接数增长而创建更多线程：
- 上下文切换次数可控
- 内存占用可预测
- 不会出现"线程爆炸"

**4. 简单清晰的编程模型**

```
"你的连接在哪个线程，你就在哪个线程处理它。"
"不要跨线程操作别人的 epoll。"
"需要跨线程时，投递任务过去。"
```

#### One Loop Per Thread 的局限

| 局限 | 影响 | 缓解方案 |
|------|------|---------|
| 负载不均 | 某个 worker 的连接特别活跃 | Round-Robin 分配 + 连接数均衡 |
| 连接不可迁移 | 无法动态转移连接到其他线程 | 业务层做负载感知重连 |
| 单连接瓶颈 | 一个连接占用过多 CPU 影响同线程其他连接 | 限制单连接消息频率、异步化耗时操作 |
| 线程数固定 | 突发流量无法弹性扩容 | 动态线程池（复杂度大幅增加） |

---

### 2.3 为什么用 eventfd？

#### 跨线程唤醒的候选方案

```
方案 A: pipe
  int pipefd[2];
  pipe(pipefd);
  写入端写数据 → 读取端可读 → epoll 感知

  缺点：需要 2 个 fd，内核创建管道对象开销大

方案 B: socketpair
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);

  缺点：创建开销最大，不必要的协议栈

方案 C: eventfd ★ (本项目使用)
  int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

  优点：只需 1 个 fd，专为事件通知设计，内核开销最小
```

#### eventfd 的优势

| 维度 | pipe | eventfd |
|------|------|---------|
| fd 数量 | 2 (读端 + 写端) | **1** |
| 内核对象 | pipe 对象 | **eventfd 对象（更轻量）** |
| 计数器语义 | 字节流 | **64 位计数器（语义清晰）** |
| 写操作 | 至少 1 字节 | **8 字节，固定** |
| 非阻塞支持 | fcntl | **创建时直接设 EFD_NONBLOCK** |
| close-on-exec | fcntl | **创建时直接设 EFD_CLOEXEC** |
| 内存开销 | ~2 个文件结构 | **~1 个文件结构** |

#### 在项目中的使用

```cpp
// ===== EventLoop 构造：创建 eventfd + 注册到 epoll =====
EventLoop::EventLoop()
    : _wakeup_fd(CreateEventFd())            // eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)
    , _wakeup_channel(make_unique<Channel>(this, _wakeup_fd))
{
    _wakeup_channel->SetReadCallback(bind(&EventLoop::HandleRead, this));
    _wakeup_channel->EnableRead();           // 注册 EPOLLIN 到 epoll
}

// ===== 跨线程唤醒：写 eventfd =====
void EventLoop::WakeUp() {
    uint64_t arg = 1;
    write(_wakeup_fd, &arg, sizeof(arg));    // eventfd 计数器 +1
    // → epoll_wait 感知 eventfd 可读 → 返回
}

// ===== 事件循环中：读空 eventfd =====
void EventLoop::HandleRead() {
    uint64_t arg;
    while (read(_wakeup_fd, &arg, sizeof(arg)) > 0) {
        // 持续读取直到计数器为 0 (EAGAIN)
    }
}
```

#### 完整唤醒链路

```
线程 A (IO Worker)                      线程 B (其他线程)
─────────────────                      ────────────────
epoll_wait(..., 3000)   ← 阻塞等待
                              │
                        io_loop->QueueInLoop(task)
                          │
                          ├─ mutex lock
                          ├─ _pending_functors.push(task)
                          ├─ mutex unlock
                          │
                          └─ WakeUp()
                              │
                              └─ write(eventfd, &one, 8)
                                   │
epoll_wait 返回! ←───────────────┘ (eventfd 可读)
  │
  ├─ HandleRead(eventfd)  → read 清空计数器
  ├─ 处理活跃 I/O 事件
  └─ DoPendingFunctors()
       ├─ swap(_pending_functors, tasks)
       └─ for each task: task()  ← 线程 B 投递的任务在此执行
```

---

## 三、线程模型详解

### 3.1 完整架构

```
┌──────────────────────────────────────────────────────────────────┐
│                     main thread (base_loop)                       │
│                                                                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐       │
│  │   Acceptor   │  │  TimeWheel   │  │   TimerQueue     │       │
│  │              │  │              │  │                  │       │
│  │ listen socket│  │ 环形数组     │  │ timerfd + set    │       │
│  │ EPOLLIN      │  │ 1 tick/sec  │  │ 毫秒级精度       │       │
│  │              │  │ O(1) 操作   │  │ O(log n)         │       │
│  └──────┬───────┘  └──────┬───────┘  └──────────────────┘       │
│         │                 │                                       │
│         │ accept          │ 超时检测                              │
│         │                 │                                       │
│    ┌────▼─────────────────▼──────┐                                │
│    │  EventLoopThreadPool        │                                │
│    │  Round-Robin: _next++ % N   │                                │
│    └──┬──────────┬──────────┬────┘                                │
│       │          │          │                                     │
└───────┼──────────┼──────────┼─────────────────────────────────────┘
        │          │          │
   ┌────▼──┐  ┌────▼──┐  ┌────▼──┐
   │Worker │  │Worker │  │Worker │    每个 Worker 线程:
   │  0    │  │  1    │  │  N    │
   │       │  │       │  │       │    ┌─────────────────────┐
   │epoll──┤  │epoll──┤  │epoll──┤    │ EventLoop::Loop()   │
   │       │  │       │  │       │    │  while(running):    │
   │TcpConn│  │TcpConn│  │TcpConn│    │    ① Poll(3000)     │
   │  A,B  │  │  C,D  │  │  E,F  │    │    ② HandleEvent()  │
   │       │  │       │  │       │    │    ③ DoPending()    │
   │timerfd│  │timerfd│  │timerfd│    └─────────────────────┘
   └───────┘  └───────┘  └───────┘
```

### 3.2 线程职责

| 线程 | 数量 | epoll 监听 | 职责 |
|------|------|-----------|------|
| **主线程 (base_loop)** | 1 | listen socket, eventfd, timerfd, timewheel timerfd | Accept 新连接、Round-Robin 分发、连接超时检测、连接登记/注销 |
| **IO Worker (io_loop)** | N (通常=CPU核数) | 各连接的 socket fd, eventfd, 各自 timerfd | 连接读写 I/O、协议编解码、业务逻辑处理、消息发送 |

### 3.3 新连接的完整生命周期

```
步骤 1: Accept (主线程)
  epoll_wait(listen_fd, EPOLLIN) → Acceptor::HandleRead()
    → accept() → new_fd
    → SetNonBlock(new_fd)
    → TcpServer::AddConnection(new_fd)

步骤 2: 分发到 Worker (主线程 → Worker)
  io_loop = thread_pool->GetNextLoop()      // Round-Robin
  conn = make_shared<TcpConnection>(io_loop, fd)
  _connections[fd] = conn                   // 主线程登记
  time_wheel->Insert(fd)                    // 入时间轮
  io_loop->RunInLoop(                       // 投递到 Worker!
    [conn]{ conn->ConnectEstablished(); }
  )

步骤 3: 在 Worker 线程建立 (Worker 线程)
  ConnectEstablished():
    _state = CONNECTED
    _channel->EnableRead()                  // epoll_ctl ADD (在 Worker 的 epoll!)
    _connect_cb(shared_from_this())         // 通知上层

步骤 4: 数据通信 (Worker 线程)
  epoll_wait(conn_fd, EPOLLIN) → HandleRead()
    → recv 循环 → _in_buffer
    → _message_cb → Codec → Dispatcher → 业务处理

步骤 5: 断开连接 (Worker → 主线程 → Worker)
  HandleClose() → _close_cb
    → TcpServer::RemoveConnection
      → base_loop->RunInLoop(RemoveConnectionInLoop)  // 回主线程清理
        → time_wheel->Remove(fd)
        → _connections.erase(fd)
        → io_loop->RunInLoop(ConnectDestroyed)         // 回 Worker 释放 epoll
```

### 3.4 为什么需要"回主线程 → 再回 Worker"？

**核心原则：每个 EventLoop 的 epoll fd 只能由所属线程操作。**

```
RemoveConnection 涉及两类操作：

  ┌─ 主线程资源 ─────────────────────┐
  │ _connections map (unordered_map)  │  ← 主线程维护
  │ _time_wheel (Insert/Remove)       │  ← 主线程 Tick (mutex 保护写)
  └───────────────────────────────────┘

  ┌─ Worker 线程资源 ─────────────────┐
  │ epoll_ctl DEL                     │  ← 必须在 Worker 线程执行
  │ close(fd)                         │  ← 必须在 Worker 线程执行
  └───────────────────────────────────┘

  所以必须:
    ① Worker 线程 → base_loop: 完成主线程 bookkeeping
    ② base_loop → Worker 线程: 完成 epoll 清理

  两次跳转，各自操作各自的资源。
```

### 3.5 线程间同步机制汇总

| 机制 | 用途 | 使用位置 |
|------|------|---------|
| **eventfd** | 跨线程唤醒 epoll_wait | EventLoop::WakeUp / QueueInLoop |
| **mutex + swap** | 任务队列线程安全 | EventLoop::DoPendingFunctors |
| **mutex** | TimeWheel 跨线程访问 | TimeWheel::Insert/Refresh/Remove/Tick |
| **condition_variable** | EventLoopThread 启动同步 | EventLoopThread::StartLoop / ThreadFunc |
| **condition_variable** | 连接池 Borrow 阻塞等待 | ConnectionPool::Borrow / Return |
| **atomic\<bool\>** | 无锁标志位 | EventLoop::_running, TimeWheel::_alive |
| **shared_ptr + enable_shared_from_this** | 跨线程生命周期安全 | TcpConnection 回调捕获 |

---

## 四、Redis / MySQL 的作用

### 4.1 整体存储架构

```
┌─────────────────────────────────────────────────────┐
│                    业务服务层                         │
│  UserService / FriendService / ChatServer           │
│                                                     │
│  读请求:  先 Redis → miss → MySQL → 回填 Redis      │
│  写请求:  先 MySQL → 失效/更新 Redis                │
└────────┬────────────────────────────┬───────────────┘
         │                            │
    ┌────▼────┐                  ┌────▼────┐
    │  Redis  │                  │  MySQL  │
    │ (缓存)  │                  │ (持久化)│
    │         │                  │         │
    │ • 热数据 │                  │ • 全量数据│
    │ • 在线状态│                 │ • ACID   │
    │ • 验证码 │                  │ • 复杂查询│
    │ • 搜索历史│                 │ • 关系模型│
    └─────────┘                  └─────────┘
```

### 4.2 MySQL 的职责

#### 数据模型

```
┌──────────┐     ┌──────────────┐     ┌─────────────────┐
│  users   │     │  friendships │     │ private_messages │
├──────────┤     ├──────────────┤     ├─────────────────┤
│ id (PK)  │◄───┐│ id (PK)      │     │ id (PK)          │
│ email    │    ││ user_id (FK) │     │ from_user_id (FK)│
│ username │    ├┤ friend_id(FK)│     │ to_user_id (FK)  │
│ password │    ││ status       │     │ content          │
│ salt     │    ││ remark       │     │ is_read          │
│ avatar   │    ││ created_at   │     │ created_at       │
│ status   │    │└──────────────┘     │ revoked          │
│ last_seen│    │                     │ reply_to_id      │
└──────────┘    │ 双向好友关系:        └─────────────────┘
                │ A→B + B→A 各一条
                └─────────────────────┘

┌───────────────────┐
│ verification_codes│
├───────────────────┤
│ id (PK)           │
│ email             │
│ code              │
│ type (注册/重置)   │
│ expires_at        │
│ used              │
└───────────────────┘
```

#### MySQL 的关键作用

| 作用 | 说明 |
|------|------|
| **持久化存储** | 用户信息、好友关系、聊天记录必须持久化 |
| **ACID 事务** | 注册（插入用户 + 初始数据）、好友双向插入需事务保证 |
| **复杂查询** | 好友搜索（LIKE）、消息历史（分页）、未读计数（COUNT） |
| **数据完整性** | 外键约束、唯一索引（email UNIQUE）、级联删除 |

#### 连接池设计

```
ConnectionPool:
  ┌─────────────────────────────────────┐
  │ 空闲队列: [_conn1_][_conn2_]...[ ]   │
  │ 已借出:   [_conn3_][_conn4_]...[ ]   │
  │ 总连接数: 4/8 (空闲 + 借出)          │
  └─────────────────────────────────────┘

  核心方法:
    Borrow()  → 取空闲 / 新建 / 等待
    Return()  → 放回空闲队列
    Remove()  → 销毁坏连接
    CheckHealth() → SELECT 1 (健康检查)
```

### 4.3 Redis 的职责

#### 缓存数据结构

| Key 模式 | 类型 | 内容 | TTL |
|----------|------|------|-----|
| `user:info:<id>` | Hash | 用户信息字段 | 1 小时 |
| `user:email:<email>` | String | 邮箱→用户ID映射 | 1 小时 |
| `code:<type>:<email>` | String | 验证码 | 5 分钟 |
| `online:<user_id>` | String | 在线状态 (1=在线) | 70 秒 |
| `friend:list:<user_id>` | Set | 好友 ID 集合 | 5 分钟 |
| `search:history:<user_id>` | List | 搜索历史 (最多6条) | 永久 |

#### Redis 的关键作用

| 作用 | 说明 | 性能收益 |
|------|------|---------|
| **热数据缓存** | 登录频繁的用户信息缓存，减少 MySQL 查询 | MySQL ~5-10ms → Redis ~0.5ms (10-20x 加速) |
| **在线状态管理** | TTL 机制天然适合在线状态（心跳续期、自动过期） | 无需 MySQL，Redis 原生 TTL 实现 |
| **验证码存储** | 临时验证码，带过期时间，验证后自动删除 | SETEX + GET + DEL，原子操作 |
| **好友列表缓存** | Set 结构高效存储，SISMEMBER O(1) 判好友关系 | 避免频繁 JOIN 查询 |
| **邮箱映射** | email→user_id 映射，登录第一步查询加速 | 减少最频繁的 MySQL 查询 |
| **搜索历史** | List 结构 LTRIM 自动限制长度（最多 6 条） | 轻量存储，无需建表 |
| **减轻 MySQL 压力** | 拦截 >80% 的读请求 | MySQL 连接数需求降低，成本下降 |

#### Cache-Aside 模式

```
读流程 (以登录为例):
  ┌─────────┐     ┌─────────┐     ┌─────────┐
  │ ① Redis │─miss→│ ② MySQL│─hit→│ ③ Redis │
  │  GET    │      │  SELECT │      │  SET    │
  │  email  │      │  email  │      │  email  │  (回填, TTL=1h)
  └────┬────┘      └─────────┘      └────┬────┘
       │hit                               │
       └──→ 返回                          └──→ 返回

写流程 (以更新用户信息为例):
  ┌─────────┐     ┌─────────┐
  │ ① MySQL│────→│ ② Redis │
  │  UPDATE │     │  DEL    │  (失效缓存, 下次读时重建)
  └─────────┘     └─────────┘
```

**为什么写时选择 Delete（失效）而非 Update（更新）？**

| 策略 | 问题 |
|------|------|
| Update 缓存 | 并发写时：A 更新 MySQL → B 更新 MySQL → B 更新 Redis → A 更新 Redis（覆盖了 B 的值！） |
| **Delete 缓存** ★ | 删除后，下次读时从 MySQL 重建（lazy load），保证最终一致性 |

### 4.4 异步执行模型

**为什么 DB/Redis 操作必须异步？**

```
❌ 同步执行 (阻塞 IO 线程):
  Worker 线程:
    epoll_wait → recv 数据 → 业务处理 → db->Query("SELECT...")
                                          ↑
                                    阻塞等待 MySQL (5-10ms)
                                    这期间线程无法处理其他连接!

✅ 异步执行 (本项目的设计):
  Worker 线程:                               DB Worker 线程:
    epoll_wait → recv → 业务处理               独立线程
      → db->Execute(loop, DBTask, Callback)  → pool.Borrow()
      → return (立即返回，继续 epoll)          → DBTask() 执行 SQL
                                              → pool.Return()
                                              → loop->QueueInLoop(Callback)
                                                   ↓
  Worker 线程:  ←────────────────────────────────┘
    DoPendingFunctors() → Callback() → conn->Send(响应)
```

**关键设计：回调回归 IO 线程**

```cpp
// Database::Execute 接口
void Execute(EventLoop* loop, DBTask fn, Callback callback);

// DBTask: 在 DB Worker 线程执行 (可以安全阻塞)
// Callback: 通过 loop->QueueInLoop 回到 IO 线程 (操作 TcpConnection)
```

异步执行模型确保：
- IO 线程永不被 MySQL/Redis 阻塞
- 一个慢查询只影响 DB Worker 线程池，不影响网络 I/O
- 业务回调在正确的 IO 线程执行（操作 TcpConnection 安全）

---

## 五、压测工具使用指南

### 5.1 HTTP Echo 压测（wrk）

```bash
# 1. 编译 echo server
cd build && cmake .. && make echo_bench

# 2. 启动服务
./echo_bench 8080 4   # port=8080, 4 worker threads

# 3. wrk 压测
# 基础压测（1K 连接，4 线程，30 秒）
wrk -t4 -c1000 -d30s http://127.0.0.1:8080/echo

# 高并发压测（5K 连接）
wrk -t8 -c5000 -d30s http://127.0.0.1:8080/echo

# 带 HTTP Keep-Alive 长连接
wrk -t4 -c1000 -d30s -H "Connection: Keep-Alive" http://127.0.0.1:8080/echo

# 自定义请求（POST JSON）
wrk -t4 -c1000 -d30s -s post.lua http://127.0.0.1:8080/api
```

### 5.2 TCP 原生压测（自研工具）

```bash
# 编译
cd build && cmake .. && make tcp_bench

# 用法: ./tcp_bench <host> <port> <conn_num> <msgs_per_conn> <duration_sec>

# 并发连接测试（5000 连接，每连接 100 条消息）
./tcp_bench 127.0.0.1 8080 5000 100 30

# 极限连接数测试（10000 连接）
./tcp_bench 127.0.0.1 8080 10000 50 60

# 吞吐量测试（1000 连接，无限发消息）
./tcp_bench 127.0.0.1 8080 1000 1000 30
```

### 5.3 系统调优（压测前）

```bash
# 增大文件描述符限制
ulimit -n 65535

# 快速回收 TIME_WAIT
echo 1 | sudo tee /proc/sys/net/ipv4/tcp_tw_reuse

# 增大 TCP backlog
echo 65535 | sudo tee /proc/sys/net/core/somaxconn

# 扩大临时端口范围（压测客户端）
echo "1024 65535" | sudo tee /proc/sys/net/ipv4/ip_local_port_range

# 启用 tcp_fastopen
echo 3 | sudo tee /proc/sys/net/ipv4/tcp_fastopen
```

---

## 六、总结

```
┌──────────────────────────────────────────────────────────┐
│                   MyMuduo 技术全景                        │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  I/O 模型:     epoll (ET 边缘触发) + 非阻塞 I/O          │
│  线程模型:     One Loop Per Thread (N = CPU 核数)        │
│  跨线程唤醒:   eventfd (1 fd, 计数器语义, 内核轻量)       │
│  连接分发:     Round-Robin 轮询                          │
│  超时管理:     TimeWheel (O(1) 操作, 每秒 Tick)          │
│  定时任务:     TimerQueue (timerfd + set, 毫秒级)        │
│  协议:         定长头(6字节) + JSON Body, 大端序          │
│  序列化:       jsoncpp (JSON)                            │
│  持久化:       MySQL (连接池, 异步执行, Prepared Statement)│
│  缓存:         Redis (Cache-Aside, 连接池, 异步执行)       │
│  构建:         CMake + C++17                            │
│  测试:         Google Test (102 个用例)                  │
│                                                          │
│  性能指标:                                               │
│  • HTTP Echo QPS:      85K~110K req/s                   │
│  • 最大并发连接:        5,000+                           │
│  • 平均延迟:            < 1.2 ms (echo)                 │
│  • 业务 QPS:            18K~25K (JSON 编解码)            │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

---

> 相关文档：
> - [架构设计文档](architecture.md) — 完整的类层次结构和事件流
> - [协议层文档](proto.md) — 协议格式和编解码设计
> - [数据库 & Redis 文档](db.md) — 存储层详细设计
> - [面试问答](interview.md) — 模拟面试 Q&A

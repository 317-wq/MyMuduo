# 仿 Muduo 库 — 高并发 TCP 服务器从零实现

> 基于仓库 [qigezi/tcp-server](https://gitee.com/qigezi/tcp-server) 的完整源码分析，
> 梳理该项目的前置知识、架构设计、实现流程与文件依赖关系。

---

## 一、项目概览

本项目是一个 **基于 Reactor 模型的 C++ 高并发 TCP 服务器**，仿照陈硕的 muduo 网络库设计。整个库的核心代码约 **1158 行**（`server.hpp`），在此之上构建了一个 HTTP 服务器（`http.hpp`，约 847 行）作为应用示例。

### 核心能力

| 特性 | 说明 |
|------|------|
| Reactor 事件驱动 | 基于 epoll 的 I/O 多路复用 |
| 多线程架构 | One Loop Per Thread + 主线程(accept) + 从线程池(读写) |
| 非阻塞 I/O | 所有 socket 操作均为非阻塞 |
| 定时器管理 | 基于 timerfd + 时间轮，支持连接超时/非活跃销毁 |
| 线程安全 | eventfd 唤醒机制 + 任务队列 |
| 自动缓冲区 | 可自动扩容的读写缓冲区 |
| HTTP 服务 | 支持静态资源 + 正则路由 + URL 编解码 |
| 协议切换 | 运行时动态切换上层协议回调 |

---

## 二、前置知识清单

### 2.1 必知必会 — Linux 系统编程

| 知识点 | 具体内容 | 在项目中的使用位置 |
|--------|---------|-------------------|
| **文件描述符** | fd 的概念、生命周期、阻塞/非阻塞 | `Socket` 类 |
| **socket API** | `socket()`, `bind()`, `listen()`, `accept()`, `connect()` | `Socket::CreateServer/CreateClient` |
| **send/recv** | 非阻塞发送接收，`EAGAIN`, `EINTR` 错误处理 | `Socket::Recv/Send/NonBlockRecv/NonBlockSend` |
| **fcntl** | 设置文件描述符为非阻塞 `O_NONBLOCK` | `Socket::NonBlock` |
| **setsockopt** | `SO_REUSEADDR`, `SO_REUSEPORT` 地址重用 | `Socket::ReuseAddress` |
| **epoll** | `epoll_create`, `epoll_ctl`(ADD/MOD/DEL), `epoll_wait` | `Poller` 类 |
| **eventfd** | 创建、读写 eventfd，用于线程间事件通知 | `EventLoop::CreateEventFd/ReadEventfd/WeakUpEventFd` |
| **timerfd** | `timerfd_create`, `timerfd_settime`，定时器触发可读事件 | `TimerWheel::CreateTimerfd/ReadTimefd` |
| **signal** | `SIGPIPE` 信号忽略（防止向已关闭连接写入时进程退出） | `NetWork` 类 |

### 2.2 必知必会 — C++ 核心

| 知识点 | 在项目中的使用位置 |
|--------|-------------------|
| **C++11 多线程** | `std::thread`, `std::mutex`, `std::condition_variable`, `std::thread::id` |
| **智能指针** | `std::shared_ptr`(Connection 生命周期管理), `std::weak_ptr`(TimerWheel 定时器映射), `std::unique_ptr`(Channel 成员) |
| **std::function + std::bind** | 所有回调函数的绑定与存储 |
| **std::enable_shared_from_this** | `Connection` 从自身获取 shared_ptr |
| **std::vector 内存管理** | `Buffer` 类的自动扩容缓冲区 |
| **std::unordered_map** | Poller 的 Channel 映射, TcpServer 的连接管理 |
| **模板** | `Any` 类型擦除容器 |
| **typeid / type_info** | `Any` 类的运行时类型检查 |
| **右值引用 & std::move** | `Connection::Send` 中 Buffer 的移动语义 |
| **RAII** | Socket 析构自动 close, TimerTask 析构自动执行回调 |

### 2.3 网络编程概念

| 概念 | 说明 |
|------|------|
| **TCP 三次握手/四次挥手** | 理解连接建立与关闭流程 |
| **Reactor 模式** | 事件驱动 + 回调 + 非阻塞 I/O |
| **One Loop Per Thread** | 每个线程运行一个 EventLoop |
| **长连接 vs 短连接** | HTTP 协议中 Connection 头部字段的 keep-alive |
| **TCP 粘包/半包问题** | 为什么需要 Buffer 缓冲区 |
| **优雅关闭 vs 暴力关闭** | `Shutdown`(等数据发完再关) vs `Release`(立即关闭) |

---

## 三、整体架构

### 3.1 类依赖关系图

```
                          ┌─────────────┐
                          │  TcpServer  │  ← 对用户最友好的接口
                          └──────┬──────┘
                                 │ 包含
              ┌──────────────────┼──────────────────┐
              │                  │                  │
         ┌────▼─────┐     ┌─────▼──────┐    ┌──────▼──────┐
         │ Acceptor │     │LoopThread  │    │ Connection  │
         │  (监听)  │     │   Pool     │    │  (连接管理) │
         └────┬─────┘     └─────┬──────┘    └──────┬──────┘
              │                 │                   │
         ┌────▼─────┐     ┌────▼─────┐       ┌─────▼──────┐
         │ EventLoop│◄─── │LoopThread│       │   Buffer   │
         │ (主线程) │     └──────────┘       │   Socket   │
         └────┬─────┘                        │   Channel  │
              │                              │  EventLoop │
    ┌─────────┼──────────┐                   └────────────┘
    │         │          │
┌───▼──┐ ┌───▼───┐ ┌───▼──────┐
│Poller│ │Timer  │ │Task Queue│
│(epoll)│ │Wheel  │ │ (vector) │
└──────┘ └───────┘ └──────────┘
```

### 3.2 线程模型

```
主线程 (baseloop)
    │
    ├── Acceptor (监听 socket → accept → 分配连接到从线程)
    │
    └── LoopThreadPool
         ├── Thread-1: EventLoop (Poller + TimerWheel + TaskQueue)
         ├── Thread-2: EventLoop (Poller + TimerWheel + TaskQueue)
         └── Thread-N: EventLoop (Poller + TimerWheel + TaskQueue)
```

主线程负责 accept 新连接，然后通过 **Round-Robin** 策略把连接分发给从线程池。每个从线程运行独立的 EventLoop。

### 3.3 事件流

```
客户端数据到达
    → epoll_wait 返回就绪 fd
    → Channel::HandleEvent()
    → Connection::HandleRead()    // 读数据到 _in_buffer
    → _message_callback()         // 用户回调处理业务
    → Connection::Send()          // 数据写入 _out_buffer，启动写监控
    → epoll_wait 返回可写
    → Connection::HandleWrite()   // 发送数据
    → 发送完毕，关闭写监控
```

---

## 四、类的层次结构（自底向上）

### 4.1 工具层

```
Buffer           — 自动扩容的读写缓冲区（基于 std::vector<char>）
Any              — 类型擦除容器（类似 std::any，C++11 实现）
LOG 宏          — 带时间戳/线程ID/文件行号的日志输出
```

### 4.2 系统调用封装层

```
Socket           — socket 的 RAII 封装
    ├── Create()         socket()
    ├── Bind()           bind()
    ├── Listen()         listen()
    ├── Accept()         accept()
    ├── Connect()        connect()
    ├── Recv/Send        阻塞读写
    ├── NonBlockRecv/Send  非阻塞读写（MSG_DONTWAIT）
    ├── CreateServer()   组合：socket+bind+listen+非阻塞+地址重用
    ├── CreateClient()   组合：socket+connect
    └── Close()           close()
```

### 4.3 Reactor 核心层

```
Channel         — 文件描述符的事件管理器
    ├── 存储关心的 events (EPOLLIN/EPOLLOUT)
    ├── 5 个回调: ReadCallback, WriteCallback, ErrorCallback, CloseCallback, EventCallback
    ├── EnableRead/Write, DisableRead/Write
    └── HandleEvent() — 根据 _revents 分发回调

Poller          — epoll 封装
    ├── UpdateEvent()   — epoll_ctl(ADD/MOD)
    ├── RemoveEvent()   — epoll_ctl(DEL)
    ├── Poll()          — epoll_wait，返回活跃 Channel 列表
    └── _channels       — fd → Channel* 的映射表

EventLoop       — 事件循环（核心中的核心）
    ├── _poller        — Poller 实例
    ├── _event_fd      — eventfd（用于跨线程唤醒）
    ├── _tasks         — 任务队列（线程安全，_mutex 保护）
    ├── _timer_wheel   — 时间轮定时器
    ├── Start()        — 主循环: Poll → HandleEvent → RunAllTask
    ├── RunInLoop()    — 如果在当前线程直接执行，否则压入队列
    ├── QueueInLoop()  — 任务入队 + 唤醒 epoll
    └── IsInLoop()     — 判断当前线程是否是 EventLoop 所属线程

TimerWheel      — 时间轮定时器（60槽，每槽1秒）
    ├── 内部使用 timerfd_create 创建定时器 fd
    ├── timerfd 注册到 Channel，每秒触发一次
    ├── OnTime() → RunTimerTask() → 清空当前槽 → 析构TimerTask → 执行回调
    └── TimerAdd/Refresh/Cancel — 定时任务的增删改

TimerTask       — 单个定时任务
    ├── _timeout       — 延迟秒数
    ├── _canceled      — 是否已取消
    ├── _task_cb       — 超时执行的回调
    └── ~TimerTask()   — 析构时：未取消则执行回调；调用 _release 清理映射
```

### 4.4 线程管理层

```
LoopThread      — 一个线程 + 一个 EventLoop
    ├── ThreadEntry()  — 创建 EventLoop，通知条件变量，进入 Start() 循环
    └── GetLoop()      — 阻塞等待 EventLoop 创建完毕，返回指针

LoopThreadPool  — 从属线程池
    ├── Create()       — 创建 count 个 LoopThread
    └── NextLoop()     — Round-Robin 取出下一个 EventLoop
```

### 4.5 连接层

```
Connection : public enable_shared_from_this<Connection>
    ├── _conn_id       — 连接唯一ID（也用作定时器ID）
    ├── _sockfd        — socket 文件描述符
    ├── _socket        — Socket 对象
    ├── _channel       — Channel 对象（事件管理）
    ├── _in_buffer     — 输入缓冲区
    ├── _out_buffer    — 输出缓冲区
    ├── _context       — Any 类型上下文（用于协议切换等）
    ├── _statu         — 连接状态: DISCONNECTED/CONNECTING/CONNECTED/DISCONNECTING
    ├── HandleRead()   — 读数据 → 写 _in_buffer → 调 _message_callback
    ├── HandleWrite()  — 发送 _out_buffer → 没有待发数据则关闭写监控
    ├── HandleClose()  — 触发关闭
    ├── HandleEvent()  — 刷新定时器活跃度 + 用户事件回调
    ├── Established()  — 设置为 CONNECTED 状态 + 启动读监控 + 回调
    ├── Send()         — 数据放入 _out_buffer + 启动写监控
    ├── Shutdown()     — 半关闭：等数据发完再真正关闭
    ├── Release()      — 实际释放：移除监控 + 关闭 fd + 回调
    └── Upgrade()      — 运行时协议切换（更换回调+上下文）

Acceptor        — 监听器
    ├── HandleRead()   — accept 新连接 → _accept_callback(newfd)
    └── Listen()       — 启动读事件监控
```

### 4.6 服务层

```
TcpServer       — TCP 服务端（用户直接使用的接口）
    ├── _acceptor    — Acceptor 对象
    ├── _baseloop    — 主线程 EventLoop
    ├── _pool        — LoopThreadPool 从属线程池
    ├── _conns       — 所有连接的 shared_ptr 映射表
    ├── NewConnection()         — 为新连接创建 Connection 对象
    ├── RemoveConnection()      — 从 _conns 中移除
    ├── SetThreadCount()        — 设置从属线程数
    ├── EnableInactiveRelease() — 开启非活跃超时销毁
    ├── SetXxxCallback()        — 设置各种回调
    └── Start()                 — 创建从属线程 + 启动主循环
```

### 4.7 HTTP 应用层（基于 TcpServer）

```
HttpServer      — HTTP 服务器
    ├── _server        — TcpServer 实例
    ├── _get/post/put/delete_route  — 路由表（正则 → 处理函数）
    ├── _basedir       — 静态资源根目录
    ├── OnMessage()    — 解析 HTTP 请求 → 路由分发 → 组织响应
    ├── Get/Post/Put/Delete() — 注册路由
    └── Listen()       — 启动服务器

HttpContext     — HTTP 请求解析状态机
    └── RECV_HTTP_LINE → RECV_HTTP_HEAD → RECV_HTTP_BODY → RECV_HTTP_OVER

HttpRequest     — HTTP 请求对象
HttpResponse    — HTTP 响应对象
Util            — 工具类：字符串分割、文件读写、URL编解码、路径验证
```

---

## 五、分步实现流程

### 第 1 步：Buffer — 自动扩容缓冲区

**目标**：实现一个可自动扩容的读写缓冲区。

| 关键点 | 说明 |
|--------|------|
| 底层存储 | `std::vector<char>`，初始大小 1024 |
| 读/写偏移 | `_reader_idx` / `_writer_idx` |
| 可读大小 | `_writer_idx - _reader_idx` |
| 可写空间 | `TailIdleSize()` — 末尾空闲 |
| 自动扩容 | `EnsureWriteSpace()` — 尾空不够 + 头空不够 → resize |
| 数据迁移 | 如果尾空不够但(尾空+头空)够 → 把可读数据 memmove 到头部 |
| 查找换行 | `memchr` 查找 `\n`，用于 HTTP 协议解析 |

**依赖**：仅 `<vector>`, `<cassert>`, `<cstring>`

### 第 2 步：Socket — socket 系统调用封装

**目标**：RAII 方式的 socket 操作封装。

| 接口 | 系统调用 | 注意事项 |
|------|---------|---------|
| `Create()` | `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` | |
| `Bind()` | `bind()` + `sockaddr_in` | |
| `Listen()` | `listen()` | 默认 backlog=1024 |
| `Accept()` | `accept()` | |
| `Connect()` | `connect()` | 用于客户端 |
| `Recv()` | `recv()` | 处理 EAGAIN/EINTR |
| `Send()` | `send()` | 处理 EAGAIN/EINTR |
| `NonBlockRecv/Send` | recv/send + `MSG_DONTWAIT` | 非阻塞操作 |
| `CreateServer()` | 组合：socket+bind+listen+NonBlock+ReuseAddress | |
| `Close()` | `close()` | 析构时自动调用 |
| `NonBlock()` | `fcntl(O_NONBLOCK)` | 设置为非阻塞 |
| `ReuseAddress()` | `setsockopt(SO_REUSEADDR/REUSEPORT)` | 避免 TIME_WAIT 问题 |

**依赖**：`<fcntl.h>`, `<unistd.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<sys/socket.h>`

### 第 3 步：Channel — 文件描述符事件管理器

**目标**：封装一个 fd 需要监控的事件及事件触发后的回调。

| 关键点 | 说明 |
|--------|------|
| 成员 | `_fd`, `_events`(关心的事件), `_revents`(实际触发的事件), `EventLoop*` |
| 5 个回调 | `_read_callback`, `_write_callback`, `_error_callback`, `_close_callback`, `_event_callback` |
| `EnableRead/Write()` | 添加事件标志 + `Update()` 通知 Poller |
| `HandleEvent()` | 根据 `_revents` 分发到对应回调 — **核心事件分发函数** |
| `Remove()` | 从 Poller 移除监控 |

**依赖**：EventLoop（前向声明）

### 第 4 步：Poller — epoll 封装

**目标**：封装 epoll 的创建、添加、修改、删除、等待操作。

| 接口 | 说明 |
|------|------|
| `Poller()` | `epoll_create(MAX_EPOLLEVENTS)` |
| `UpdateEvent(channel)` | 首次添加 → `EPOLL_CTL_ADD`；已有 → `EPOLL_CTL_MOD` |
| `RemoveEvent(channel)` | `EPOLL_CTL_DEL` |
| `Poll(active*)` | `epoll_wait` 阻塞等待，将活跃 Channel 指针输出到 `active` 列表 |
| `_channels` | `unordered_map<int, Channel*>` fd→Channel 映射 |

**依赖**：Channel, `<sys/epoll.h>`

### 第 5 步：TimerWheel — 时间轮定时器

**目标**：基于 timerfd + 时间轮的高效定时器。

| 关键点 | 说明 |
|--------|------|
| 时间轮 | 60 个槽，每个槽代表 1 秒，最大支持 60 秒延迟 |
| `_tick` | 当前指针位置，每秒 +1 |
| `timerfd_create` | `CLOCK_MONOTONIC`，每秒触发一次可读事件 |
| `OnTime()` | 读取 timerfd 的计数器得到超时次数，逐次执行 `RunTimerTask()` |
| `RunTimerTask()` | `_tick++`，清空 `_wheel[_tick]` → 析构 PtrTask → 执行回调 |
| `TimerAdd()` | 在 `(_tick + delay) % 60` 位置放入 TimerTask 的 shared_ptr |
| `TimerRefresh()` | 将已有的定时任务重新插入时间轮（用于连接活跃续期） |
| `TimerCancel()` | 将任务的 `_canceled` 设为 true，析构时不执行回调 |
| `RemoveTimer()` | 从 `_timers` 中删除，由 `TimerTask` 析构时的 `_release` 回调调用 |
| **线程安全** | 所有对外接口通过 `EventLoop::RunInLoop()` 投递到 EventLoop 线程执行 |

**依赖**：EventLoop（前向声明）, Channel, `<sys/timerfd.h>`, `<memory>`

### 第 6 步：EventLoop — 事件循环

**目标**：核心事件循环，融合 Poller + TimerWheel + 任务队列。

伪代码：
```
while(1) {
    actives = poller.Poll();      // 1. 事件监控
    for each channel in actives:  // 2. 就绪事件处理
        channel.HandleEvent();
    RunAllTask();                 // 3. 执行任务队列
}
```

| 关键点 | 说明 |
|--------|------|
| `_event_fd` | eventfd，用于其他线程唤醒 epoll_wait 阻塞 |
| `_tasks` | `vector<Functor>`，线程安全（mutex + swap 缩临界区） |
| `IsInLoop()` | 判断当前线程是否是 EventLoop 所在线程 |
| `RunInLoop(cb)` | 本线程直接执行，其他线程则放入任务队列并唤醒 |
| `QueueInLoop(cb)` | 加锁 → push → 解锁 → `WeakUpEventFd()` |
| `WeakUpEventFd()` | 向 eventfd 写入 8 字节 |
| `RunAllTask()` | 加锁 → swap 取出 → 解锁 → 逐个执行 |

**线程安全核心思想**：
- 所有的 fd 事件监控/处理都在 EventLoop 线程
- 其他线程想操作 EventLoop 中的资源时，通过 `RunInLoop()` 投递任务
- `WeakUpEventFd()` 确保 epoll_wait 能立刻返回并处理任务

**依赖**：Poller, TimerWheel, Channel, `<sys/eventfd.h>`, `<thread>`, `<mutex>`

### 第 7 步：LoopThread & LoopThreadPool

**目标**：实现 One Loop Per Thread。

| 类 | 说明 |
|----|------|
| `LoopThread` | 构造函数创建线程，线程中创建 EventLoop，条件变量通知，进入 `Start()` |
| `LoopThreadPool` | 管理多个 LoopThread，`NextLoop()` 以 Round-Robin 返回 EventLoop 指针 |

**线程同步**：`GetLoop()` 中使用条件变量等待，确保 EventLoop 创建完成后才返回指针。

**依赖**：EventLoop, `<thread>`, `<mutex>`, `<condition_variable>`

### 第 8 步：Connection — 连接管理

**目标**：封装一个 TCP 连接的生命周期管理。

| 关键点 | 说明 |
|--------|------|
| 继承 | `enable_shared_from_this<Connection>` — 从成员函数获取自身 shared_ptr |
| `HandleRead()` | `NonBlockRecv` → `_in_buffer` → 若可读 > 0 → `_message_callback` |
| `HandleWrite()` | `NonBlockSend` → `MoveReadOffset` → 无待发数据则 `DisableWrite` |
| `HandleClose/Error()` | 处理残留数据后 `Release()` |
| `HandleEvent()` | `TimerRefresh`(活跃续期) + `_event_callback`(用户回调) |
| `Send(data, len)` | 数据拷入临时 Buffer → `RunInLoop(SendInLoop)` → 放入 `_out_buffer` → 启动写监控 |
| 为什么 Send 要拷贝 | 外部传入的 data 可能是临时的，任务被延迟执行时原内存可能已失效 |
| `Shutdown()` | 置状态为 DISCONNECTING → 等数据发完再 `Release()` |
| `Release()` | 移除 Channel + close fd + 取消定时器 + 回调通知 |
| `EnableInactiveRelease(sec)` | 设置定时任务，sec 秒后若未刷新则自动 `Release()` |

**4 种连接状态**：
```
DISCONNECTED → CONNECTING → CONNECTED → DISCONNECTING → DISCONNECTED
```

**依赖**：Buffer, Socket, Channel, EventLoop, Any

### 第 9 步：Acceptor — 监听器

**目标**：封装对新连接的监听与分发。

| 关键点 | 说明 |
|--------|------|
| `HandleRead()` | `accept()` 新连接 → `_accept_callback(newfd)` |
| `Listen()` | 启动监听 socket 的读事件监控 |
| **注意** | 必须在设置回调后再 `Listen()`，否则可能丢失连接 |

**依赖**：Socket, Channel, EventLoop

### 第 10 步：TcpServer — 服务端封装

**目标**：将所有组件整合，对外提供简单易用的接口。

伪代码：
```
TcpServer(port):
    Acceptor 绑定 baseloop
    Acceptor 设置回调 NewConnection

Start():
    pool.Create()          // 创建从属线程
    baseloop.Start()       // 启动主循环

NewConnection(fd):
    conn = new Connection(pool.NextLoop(), next_id, fd)
    设置各种回调到 conn
    if 开启非活跃超时: conn.EnableInactiveRelease(timeout)
    conn.Established()     // 设置为 CONNECTED + 启动读监控
    _conns[id] = conn

RemoveConnection(conn):
    baseloop.RunInLoop → _conns.erase(conn.Id())
```

**依赖**：所有上述组件

### 第 11 步：HTTP 应用层

**目标**：基于 TcpServer 实现完整的 HTTP 服务器。

需要额外实现：
1. **HttpRequest/HttpResponse** — 请求/响应对象
2. **HttpContext** — 状态机解析 HTTP 请求
   - `RECV_HTTP_LINE` → `RECV_HTTP_HEAD` → `RECV_HTTP_BODY` → `RECV_HTTP_OVER`
3. **Util** — 工具类（字符串分割、文件读/写、URL 编解码、路径安全验证、MIME 映射、状态码描述）
4. **HttpServer** — 路由表（正则匹配）+ 静态资源服务 + 错误处理

### 第 12 步：Any — 类型擦除容器（可选）

**目标**：实现类似 C++17 `std::any` 的容器。

| 关键点 | 说明 |
|--------|------|
| 设计模式 | 类型擦除 + 模板子类 `placeholder<T>` 继承自 `holder` 基类 |
| `get<T>()` | typeid 校验类型匹配，返回数据指针 |
| `clone()` | 深拷贝，用于拷贝构造和赋值 |

在项目中的作用：`Connection::_context` 存储任意协议上下文（如 HttpContext）。

---

## 六、文件组织与依赖

```
你的项目目录/
├── source/
│   └── server.hpp          # ★ 核心库（必须），1158行，包含所有Reactor组件
├── source/echo/
│   ├── echo.hpp            # EchoServer 示例（include server.hpp）
│   ├── main.cc             # 入口
│   └── Makefile
├── source/http/
│   ├── http.hpp            # HTTP 服务器（include server.hpp）
│   ├── main.cc             # 入口
│   ├── mime                # MIME 映射表（已被 http.hpp 内联）
│   ├── statu               # 状态码表（已被 http.hpp 内联）
│   ├── wwwroot/            # 静态资源根目录
│   └── Makefile
├── example/                # 各知识点的独立示例（可选参考）
│   ├── any.cpp             # Any 类使用示例
│   ├── bind.cpp            # std::bind 使用示例
│   ├── eventfd.c           # eventfd 使用示例
│   ├── request.cpp         # HTTP 请求工具函数示例
│   ├── socket.c            # socket API 示例
│   ├── test.cpp            # 综合工具函数测试
│   ├── timerfd.cpp         # timerfd 使用示例
│   └── timewheel.cpp       # 时间轮示例
└── test/
    ├── Makefile
    ├── server.cc           # 使用 TcpServer 的测试服务端
    ├── tcp_srv.cc          # 使用底层组件的测试服务端（手动管理 Connection）
    ├── tcp_cli.cc          # 简单 TCP 客户端
    └── client1~6.cpp       # 多种客户端测试用例
```

### 文件 include 依赖

```
server.hpp  ←──  仅依赖 C++ 标准库 + POSIX 头文件，无自定义头文件依赖
    ↑
    ├── echo.hpp          (include "../server.hpp")
    ├── http.hpp          (include "../server.hpp")
    ├── test/server.cc    (include "../source/server.hpp")
    ├── test/tcp_srv.cc   (include "../source/server.hpp")
    └── test/tcp_cli.cc   (include "../source/server.hpp")
```

**核心要点**：整个项目只有一个必须的源文件 `server.hpp`，它是纯头文件库（header-only）。

### 编译方式

```bash
# g++ -std=c++11 -g main.cc -o main -lpthread

# Echo 服务器
cd source/echo && make

# HTTP 服务器
cd source/http && make

# 测试
cd test && make server && make client
```

---

## 七、关键设计要点

### 7.1 线程安全策略

| 场景 | 做法 |
|------|------|
| 跨线程操作 EventLoop 资源 | 通过 `RunInLoop(cb)` 投递任务到 EventLoop 线程执行 |
| 任务队列保护 | `std::mutex` + `swap()` 缩小临界区 |
| 跨线程唤醒 | eventfd 写入 → epoll_wait 返回 → 执行任务队列 |
| 定时器操作 | `TimerAdd/Refresh/Cancel` 内部都用 `RunInLoop` 投递 |

### 7.2 连接生命周期管理

- Connection 使用 `shared_ptr` 管理，由 `TcpServer::_conns` 和 Channel 的回调共同持有
- 当连接断开时，`ReleaseInLoop` 先调用用户的 `_closed_callback`，再调用 `_server_closed_callback`（从 `_conns` 移除），顺序保证回调执行时连接信息仍然有效

### 7.3 优雅关闭 vs 暴力关闭

- **Shutdown**：设置状态为 DISCONNECTING，等待 `_out_buffer` 中的数据全部发送完毕再真正释放
- **Release**：立即移除监控 + 关闭 fd

### 7.4 非活跃连接检测

```
每次 HandleEvent 触发 → TimerRefresh(conn_id) // 刷新定时器
如果 sec 秒内没有 HandleEvent → TimerTask 析构 → Release()
```

### 7.5 Buffer 的设计

- 不是环形缓冲区，而是可自动扩容的线性缓冲区
- 当尾部空闲不足但(尾+头)空闲足够时，把数据 memmove 回头部
- 当全部空闲不足时，直接 resize 扩容

### 7.6 SIGPIPE 处理

全局 `NetWork` 对象在程序启动时自动执行 `signal(SIGPIPE, SIG_IGN)`，防止向已断开连接写入时导致进程退出。

---

## 八、推荐的实现顺序

```
步骤 1:  Buffer       (1小时)    — 理解缓冲区原理
步骤 2:  Socket       (1小时)    — 回顾 socket API
步骤 3:  Logger       (0.5小时)  — 辅助调试
步骤 4:  Channel      (1小时)    — 事件抽象
步骤 5:  Poller       (1小时)    — epoll 封装
步骤 6:  Any          (0.5小时)  — 模板/类型擦除（可暂时跳过）
步骤 7:  TimerWheel   (1.5小时)  — timerfd + 时间轮
步骤 8:  EventLoop    (2小时)    — ★ 核心，需要仔细理解
步骤 9:  LoopThread   (1小时)    — One Loop Per Thread
步骤 10: Connection   (2小时)    — ★ 连接管理，逻辑最复杂
步骤 11: Acceptor     (0.5小时)  — 监听封装
步骤 12: TcpServer    (1小时)    — 整合所有组件
步骤 13: HTTP 应用    (3小时)    — 应用层协议实现

总计约 16 小时
```

---

## 九、延伸阅读

- [Muduo 网络库源码](https://github.com/chenshuo/muduo) — 本项目参考的原始库
- 《Linux 多线程服务端编程》— 陈硕，muduo 作者
- man 手册：`epoll(7)`, `eventfd(2)`, `timerfd_create(2)`, `fcntl(2)`
- RFC 2616 / RFC 7230 — HTTP/1.1 协议规范

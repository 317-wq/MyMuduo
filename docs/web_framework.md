# MyMuduo 网络框架设计文档

## 一、设计哲学

仿 Muduo 的 C++17 Reactor 网络库。核心思想：

- **One Loop Per Thread** — 每个线程一个 EventLoop，epoll 实例与线程一一绑定，消除 fd 跨线程竞争
- **非阻塞 I/O + LT epoll** — 水平触发，简单可靠，不会漏事件
- **回调驱动** — 上层通过设置回调（onConnection / onMessage / onClose）响应事件，框架不关心业务逻辑
- **跨线程任务分发** — 其他线程想操作某 EventLoop 的资源时，通过 `RunInLoop(cb)` 把任务投递到目标线程执行

## 二、整体架构

```
                        ┌─────────────┐
                        │  TcpServer  │  ← 用户直接使用的接口
                        └──────┬──────┘
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
    ┌─────▼──────┐   ┌────────▼───────┐   ┌────────▼──────┐
    │  Acceptor  │   │EventLoopThread │   │  TimeWheel    │
    │ (监听新连接)│   │    Pool        │   │ (连接超时检测) │
    └─────┬──────┘   └────────┬───────┘   └────────┬──────┘
          │                    │                     │
    ┌─────▼──────┐   ┌────────▼───────┐            │
    │ EventLoop  │◄──│EventLoopThread │            │
    │ (base线程) │   │ (worker线程)   │            │
    └─────┬──────┘   └────────────────┘            │
          │                                         │
    ┌─────▼──────┐                          ┌──────▼──────┐
    │  Poller    │                          │ TcpConnection│
    │ (epoll)    │                          │  (连接管理)  │
    └─────┬──────┘                          └──────┬──────┘
          │                                         │
    ┌─────▼──────┐                          ┌──────▼──────┐
    │  Channel   │                          │   Buffer     │
    │ (事件抽象) │                          │  Socket      │
    └────────────┘                          │  Channel     │
                                            │  EventLoop   │
                                            └──────────────┘
```

## 三、线程模型

```
main 线程 (base_loop)                     worker 线程 0..N (io_loop)
  │                                         │
  ├─ Acceptor (listen fd)                   ├─ EpollPoller
  │   └─ accept → AddConnection             │   ├─ TcpConnection::HandleRead
  │       └─ Round-Robin 分发到 worker ────►│   ├─ TcpConnection::HandleWrite
  │                                         │   └─ TcpConnection::HandleClose
  ├─ TimeWheel (每秒 tick)                   │
  │   └─ 超时 → HandleTimeout               ├─ TimerQueue (独立 timerfd)
  │       └─ RemoveConnection               │
  │                                         └─ DoPendingFunctors
  └─ TimerQueue (timerfd)
```

**关键约束**：每个 EventLoop 的 epoll fd 只能由归属线程操作。跨线程操作必须通过 `io_loop->RunInLoop(fn)` 投递任务。

## 四、模块说明（自底向上）

### 4.1 工具层

#### Buffer (`net/Buffer.h`)

可自动扩容的线性读写缓冲区。

- 底层 `std::vector<char>`，初始 1024 字节
- `_read_pos` / `_write_pos` 双指针，非环形
- `EnsureWritable(len)`：尾空不够 → memmove 到头部；还不够 → resize 扩容
- `Append(data, len)` 写入，`Retrieve(len)` 消费，`RetrieveAllAsString()` 取出全部
- 从 Muduo 继承的关键设计：不是环形缓冲区，避免 `read()` 时跨尾部回绕的复杂性

#### NoCopy (`base/NoCopy.h`)

禁用拷贝/移动的 mixin 基类。

#### Socket (`net/Socket.h`) + InetAddress (`net/InetAddress.h`)

RAII 封装 socket fd 和 sockaddr_in。`Socket` 析构自动 close，支持 SetNonBlock/SetReuseAddr/SetReusePort。

### 4.2 Reactor 核心

#### Channel (`net/Channel.h`)

fd 的事件管理器，是整个框架的事件抽象单元。

```
一个 Channel = 一个 fd + 关心的事件 + 4 个回调
  _fd        — 文件描述符
  _events    — 关心的事件 (EPOLLIN | EPOLLOUT)
  _revents   — epoll 返回的就绪事件
  _read_cb   — 数据可读回调
  _write_cb  — 可写回调
  _error_cb  — 错误回调
  _close_cb  — 连接关闭回调
```

- `EnableRead()/EnableWrite()` → 设置 `_events` 标志位 → `Update()` → `EventLoop::UpdateChannel()` → `epoll_ctl`
- `HandleEvent()` → 根据 `_revents` 分发给对应回调（`EPOLLIN → _read_cb`，`EPOLLOUT → _write_cb`，`EPOLLERR → _error_cb`，`EPOLLHUP → _close_cb`）
- Channel 不拥有 fd，只管理事件

#### Poller / EpollPoller (`net/Poller.h`, `net/EpollPoller.h`)

epoll 实例的封装。`Poller` 是抽象基类，`EpollPoller` 是 epoll 实现。

- `_epfd` — epoll fd（`epoll_create1(EPOLL_CLOEXEC)`）
- `_channels` — `unordered_map<int, Channel*>`，fd→Channel 映射
- `Poll(active_channels, timeout)` → `epoll_wait` → `FillActiveChannels`
- `UpdateChannel(channel)` → 首次 `EPOLL_CTL_ADD`，已有 `EPOLL_CTL_MOD`
- `RemoveChannel(channel)` → `EPOLL_CTL_DEL` + 从 `_channels` 移除

#### EventLoop (`net/EventLoop.h`)

**框架核心**，融合 Poller + TimerQueue + 任务队列。

```
主循环 (Loop):
  while (_running):
    1. Poll(3000)              — epoll_wait 等待事件
    2. for channel in actives: — 分发给 Channel::HandleEvent
         channel->HandleEvent()
    3. DoPendingFunctors()     — 执行跨线程投递的任务
```

关键机制：

| 机制 | 实现 | 作用 |
|------|------|------|
| **eventfd 唤醒** | `_wakeup_fd` + `_wakeup_channel` | 其他线程通过 `WakeUp()` 写入 eventfd 唤醒阻塞在 `epoll_wait` 的线程 |
| **任务队列** | `_pending_functors` vector | 跨线程投递的任务先入队，`DoPendingFunctors` swap 到本地再执行（缩小锁粒度） |
| **RunInLoop** | 本线程直接执行，其他线程 `QueueInLoop` | 保证所有操作在目标线程发生 |
| **IsInLoopThread** | `thread::id` 比较 | 判断当前是否在 EventLoop 所属线程 |

### 4.3 定时器

#### Timer (`net/Timer.h`)

单个定时器对象，保存到期时间点、回调、是否重复、间隔。

#### TimerQueue (`net/TimerQueue.h`)

基于 `timerfd` + `std::set<TimerPtr>`（按到期时间升序）的定时器管理。

- `timerfd_create(CLOCK_MONOTONIC, ...)` 创建定时器 fd，注册到 Channel
- `HandleRead()` → 收集所有到期 Timer → 执行回调 → 周期性 Timer 重新插入
- `ResetTimerFd()` → 设置 timerfd 的下次超时时间为最近 Timer 的到期时间
- `RunAfter(sec, cb)` → 一次性定时任务
- `RunEvery(sec, cb)` → 周期性定时任务

**注意**：当前设计没有 Timer 取消接口，周期性任务注册后无法中途取消。

#### TimeWheel (`net/TimeWheel.h`)

连接超时检测的时间轮，与 TimerQueue 解耦。

- 环形数组 `_wheel[_capacity]`，每个槽是一个 `unordered_set<int>`（存 fd）
- 每秒 Tick 一次（通过 `_loop->RunEvery(1, ...)` 注册到 TimerQueue）
- `Insert(fd)` → 放在 `(_index + capacity - 1)` 位置（最远槽）→ 最多存活 capacity 秒
- `Refresh(fd)` → 从旧槽移除 + 重新 Insert（连接有数据活动时续命）
- `Remove(fd)` → 从槽和 _fd_slot_map 中删除
- **线程安全**：`_mutex` 保护所有操作。`Tick()` 在 base 线程，`Refresh()` 在 worker 线程（OnMessage）。
- **`_alive` 标志**：shared_ptr\<bool\>，析构时置 false，Timer 回调检查此标志防止悬空调用

### 4.4 线程管理

#### EventLoopThread (`net/EventLoopThread.h`)

一个线程 + 一个 EventLoop 的封装。

- `ThreadFunc()`：创建 `EventLoop` → 条件变量通知主线程 → `Loop()` 进入事件循环
- `StartLoop()`：创建 `std::thread` → 条件变量等待 `_loop` 创建完成 → 返回 `EventLoop*`
- `~EventLoopThread()`：`Quit()` + `join()`

#### EventLoopThreadPool (`net/EventLoopThreadPool.h`)

管理 N 个 EventLoopThread。`GetNextLoop()` 用 Round-Robin 返回下一个 worker 的 EventLoop。

### 4.5 连接层

#### TcpConnection (`net/TcpConnection.h`)

一个 TCP 连接的生命周期管理。继承 `enable_shared_from_this`，生命周期由 `shared_ptr` 管理。

**状态机**：
```
CONNECTING → CONNECTED → DISCONNECTED
                ↓ (未使用)
           DISCONNECTING
```

**I/O 路径**：
```
可读事件 → HandleRead()
  → while(recv) 读到 EAGAIN → 数据写入 _in_buffer
  → _message_cb(conn, &_in_buffer)  // 通知上层处理
  → 上层在回调中调用 conn->Send() 发送响应

可写事件 → HandleWrite()
  → send(_out_buffer.Peek()) → _out_buffer.Retrieve(n)
  → enable/disable write 按需

错误/关闭 → HandleError()/HandleClose()
  → _state = DISCONNECTED → _close_cb → RemoveConnection
```

**Send() 线程安全**：`Send()` 通过 `_loop->RunInLoop` 分发到 `SendInLoop()`，保证 buffer 和 epoll 操作都在 io_loop 线程。

#### Acceptor (`net/Acceptor.h`)

监听新连接。持有 listen socket 的 Channel，`HandleRead()` 中 `accept()` 后通过回调通知 TcpServer。

### 4.6 服务层

#### TcpServer (`net/TcpServer.h`)

组合所有组件，对外提供简洁接口。

```
TcpServer 拥有:
  _acceptor      — 监听新连接
  _thread_pool   — worker 线程池
  _time_wheel    — 连接超时检测
  _connections   — fd → TcpConnection::Ptr 映射
```

**连接生命周期**：
```
1. Acceptor::HandleRead → accept → _new_connection_cb(fd)
2. TcpServer::AddConnection(fd)
   - Round-Robin 选取 worker io_loop
   - new TcpConnection(io_loop, fd)
   - 设置 connection 的三层回调
   - _time_wheel->Insert(fd)
   - io_loop->RunInLoop → conn->ConnectEstablished()
3. ConnectEstablished → _state = CONNECTED → EnableRead → _connect_cb
4. ... 数据收发 ...
5. HandleClose → _close_cb → RemoveConnection
   - dispatch 到 base_loop (bookkeeping)
   - dispatch ConnectDestroyed 到 io_loop (epoll 清理)
```

**回调层次**：
```
TcpServer 设置的回调:
  _connect_cb     → 传给 TcpConnection (通知上层新连接)
  _message_cb     → 包在 OnMessage 里传给 TcpConnection
                    OnMessage 先 Refresh TimeWheel，再调用户回调
  _close_cb       → RemoveConnection (框架内部处理)
```

**析构顺序**：
```
1. _time_wheel.reset()        — 停止超时检测
2. 拷贝 _connections → 清空 map
3. 分发 ConnectDestroyed 到各 worker
4. _thread_pool 析构          — Quit + join，保证步骤 3 的任务执行完毕
```

## 五、跨线程任务分发机制

这是框架线程安全的核心设计：

```
线程 A (非 io_loop) 想操作线程 B 的 EventLoop 资源:

  io_loop->RunInLoop(fn)
    → IsInLoopThread()? false
    → QueueInLoop(fn)
        → 加锁 push _pending_functors
        → WakeUp() 写 eventfd
    → epoll_wait 被唤醒
    → HandleEvent 处理 wakeup_channel (读空 eventfd)
    → DoPendingFunctors() 执行 fn
```

`RunInLoop` 在目标线程调用时直接执行，无需入队。

## 六、目录结构

```
include/
  base/
    Exception.h    — 框架异常 (mymuduo::Exception)
    NoCopy.h       — 禁用拷贝 mixin
  net/
    Buffer.h       — 自动扩容缓冲区
    Socket.h       — socket RAII
    InetAddress.h  — sockaddr_in 封装
    Channel.h      — fd 事件抽象
    Poller.h       — 多路复用基类
    EpollPoller.h  — epoll 实现
    EventLoop.h    — 事件循环核心
    Timer.h        — 定时器
    TimerQueue.h   — timerfd 定时器管理
    TimeWheel.h    — 连接超时时间轮
    EventLoopThread.h      — one loop per thread
    EventLoopThreadPool.h  — 线程池
    TcpConnection.h — 连接管理
    Acceptor.h      — 监听器
    TcpServer.h     — 服务端接口

src/
  base/            — 预留
  net/             — 与 include/net/ 一一对应的实现

test/              — 测试代码
bench/             — QPS/TPS/OPS 性能测试
examples/          — 使用示例
log/               — 日志输出目录
docs/              — 设计文档
```

## 七、关键设计决策

| 决策 | 理由 |
|------|------|
| 水平触发 (LT) 而非边缘触发 (ET) | LT 更简单可靠，不会漏事件；Muduo 也用 LT |
| Buffer 线性而非环形 | 避免 `read()` 跨尾部时 memmove 两次的复杂性 |
| TimeWheel 独立于 TimerQueue | 关注点分离：TimerQueue 管通用定时任务，TimeWheel 只管连接超时 |
| `shared_ptr` 管理 TcpConnection 生命周期 | 连接可能在多个地方被引用（TcpServer map、Channel 回调、用户回调），无法由单一 owner 决定释放时机 |
| `enable_shared_from_this` | 允许 HandleRead/HandleWrite 中安全地获取自身 shared_ptr 传给回调 |
| 回调中使用裸 `this` 绑定到 Channel | Channel 不拥有 TcpConnection，生命周期由 shared_ptr 在 TcpServer::_connections 中保证 |
| eventfd 而非 pipe 做唤醒 | eventfd 更轻量，内核态只需维护 8 字节计数器 |
| timerfd 而非 alarm/setitimer | timerfd 是 fd，可以统一纳入 epoll 管理，线程安全 |

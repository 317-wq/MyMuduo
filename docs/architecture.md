# MyMuduo 架构设计文档

> 本文档记录 MyMuduo 网络库的整体架构设计，便于复习和面试准备。

---

## 一、设计理念

### 1.1 项目定位

基于 **Reactor 模式** 的 C++17 高并发 TCP 服务器框架，核心设计思想参考陈硕的 Muduo 网络库：

- **非阻塞 I/O + I/O 多路复用（epoll）**
- **One Loop Per Thread 线程模型**
- **事件驱动 + 回调机制**

在此网络框架之上，构建了一个完整的 **即时通讯（聊天室）应用**，包含自定义协议、MySQL + Redis 存储、在线状态管理等业务层实现。

### 1.2 核心设计原则

| 原则 | 说明 |
|------|------|
| **每个 EventLoop 的 epoll fd 只能由所属线程操作** | 跨线程操作通过 `RunInLoop(fn)` 投递任务 |
| **TcpConnection 使用 `shared_ptr` 管理生命周期** | 继承 `enable_shared_from_this`，避免悬空指针 |
| **回调分层解耦** | 网络层 → 协议层 → 业务层，各层通过回调串联 |
| **线程安全的任务投递机制** | eventfd 唤醒 + mutex + swap 缩临界区 |
| **RAII 管理资源** | Socket、EventLoop、TimerQueue 等均自动管理 fd 生命周期 |

---

## 二、线程模型：One Loop Per Thread

### 2.1 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                      main thread (base_loop)                 │
│                                                              │
│  ┌──────────┐   ┌──────────┐   ┌───────────────┐           │
│  │ Acceptor │   │TimeWheel │   │  TimerQueue   │           │
│  │ (listen) │   │(1 tick/s)│   │ (timerfd)     │           │
│  └────┬─────┘   └────┬─────┘   └───────────────┘           │
│       │              │                                       │
│       │    accept    │ 超时检测                               │
│       │    Round-Robin 分发                                   │
└───────┼──────────────┼───────────────────────────────────────┘
        │              │
   ┌────▼──────────────▼────┐
   │   EventLoopThreadPool  │
   │   (Round-Robin 轮询)   │
   └──┬───────┬───────┬─────┘
      │       │       │
┌─────▼──┐ ┌──▼───┐ ┌─▼─────┐
│Worker 0│ │ ...  │ │Worker N│     每个 worker:
│        │ │      │ │        │       ├─ EventLoop → EpollPoller
│epoll ──┤ │      │ │        │       ├─ TcpConnection::HandleRead
│conn fd │ │      │ │        │       ├─ TcpConnection::HandleWrite
│timerfd │ │      │ │        │       └─ DoPendingFunctors
└────────┘ └──────┘ └────────┘
```

### 2.2 线程职责

| 线程 | 职责 | 关键操作 |
|------|------|---------|
| **主线程 (base_loop)** | 监听 accept、超时检测、连接登记 | `Acceptor::HandleRead` → `AddConnection` → 分发到 worker<br>`TimeWheel::Tick` → 超时连接回调 |
| **Worker 线程 (io_loop)** | 处理已建立连接的所有 I/O | `HandleRead` → 读数据到 in_buffer<br>`HandleWrite` → 从 out_buffer 发送<br>`DoPendingFunctors` → 执行跨线程投递的任务 |

### 2.3 跨线程调度机制

```
其他线程想操作 Worker 的 epoll 资源:
    │
    ├─ io_loop->RunInLoop(fn)
    │       │
    │       ├─ IsInLoopThread()? → 是 → 直接执行 fn
    │       └─ 否 → QueueInLoop(fn)
    │                   │
    │                   ├─ mutex lock → _pending_functors.push(fn)
    │                   └─ WakeUp() → write(eventfd, 1)
    │                                       │
    │                              epoll_wait 返回（eventfd 可读）
    │                                       │
    │                              HandleRead(eventfd) → 读空计数器
    │                                       │
    │                              DoPendingFunctors()
    │                                  ├─ mutex lock → swap(_pending_functors, tasks)
    │                                  └─ for each fn in tasks: fn()
```

**为什么用 eventfd？**
- eventfd 是一个可以放入 epoll 监听的文件描述符
- 其他线程向 eventfd write 数据 → epoll_wait 立即返回 → 处理任务队列
- 相比 pipe，eventfd 只需要一个 fd（pipe 需要两个），内核开销更小

---

## 三、类层次结构（自底向上）

### 3.1 系统调用封装层

```
Socket              — socket fd 的 RAII 封装
  ├─ socket() / bind() / listen() / accept() / connect()
  ├─ SetNonBlock()  — fcntl(O_NONBLOCK)
  ├─ SetReuseAddr() — setsockopt(SO_REUSEADDR | SO_REUSEPORT)
  └─ Close()        — 析构时自动 close(fd)

InetAddress         — sockaddr_in 封装，支持 IPv4 地址和端口
```

### 3.2 I/O 多路复用层

```
Channel             — fd 的事件管理器
  ├─ _events:  关心的事件 (EPOLLIN | EPOLLOUT)
  ├─ _revents: 实际触发的事件
  ├─ 5 个回调: Read / Write / Error / Close / Event
  ├─ EnableRead/Write → _events |= EPOLLIN/OUT → Update() → EventLoop::UpdateChannel
  └─ HandleEvent() → 根据 _revents 分发回调

Poller (抽象基类)
  └─ EpollPoller    — epoll 封装
       ├─ epoll_create1(EPOLL_CLOEXEC)
       ├─ UpdateChannel → epoll_ctl(ADD|MOD)
       ├─ RemoveChannel → epoll_ctl(DEL) + 从 _channels map 移除
       ├─ Poll(timeout) → epoll_wait → FillActiveChannels
       └─ _events 数组支持动态扩容（2x）
```

**Channel ↔ EventLoop ↔ EpollPoller 的协作流程：**

```
Channel::EnableRead()
  → EventLoop::UpdateChannel(channel)
    → EpollPoller::UpdateChannel(channel)
      → epoll_ctl(EPOLL_CTL_ADD, fd, EPOLLIN)
      → _channels[fd] = channel

epoll_wait 返回活跃 fd
  → FillActiveChannels: channel->SetREvents(revents)
  → EventLoop 遍历 _active_channels
    → channel->HandleEvent()
      → if _revents & EPOLLIN → _read_cb()
      → if _revents & EPOLLOUT → _write_cb()
      → if _revents & EPOLLERR → _error_cb()
      → if _revents & EPOLLHUP → _close_cb()
```

### 3.3 事件循环层

```
EventLoop           — ★ 核心中的核心
  ├─ _poller (EpollPoller)    — epoll 实例
  ├─ _wakeup_fd (eventfd)     — 跨线程唤醒
  ├─ _wakeup_channel          — 监听 eventfd 的 Channel
  ├─ _pending_functors        — 任务队列 (mutex 保护)
  ├─ _timer_queue             — timerfd 定时器
  │
  ├─ Loop()   — 主循环:
  │     while(_running):
  │       Poll(3000)           — epoll_wait，超时 3 秒
  │       for channel in actives: channel->HandleEvent()
  │       DoPendingFunctors()  — 执行跨线程投递的任务
  │
  ├─ RunInLoop(fn)      — 同线程直接执行，否则 QueueInLoop
  ├─ QueueInLoop(fn)    — 入队 + WakeUp()
  ├─ RunAfter(sec, cb)  — 一次性定时任务
  ├─ RunEvery(sec, cb)  — 周期性定时任务
  └─ Quit()             — 停止循环 + WakeUp()
```

**为什么 Poll 超时设为 3000ms？**

因为 `TimerQueue` 使用 `timerfd` 机制——当定时任务到期时，timerfd 变为可读，epoll_wait 会被唤醒。所以 epoll_wait 的超时只是兜底机制，不影响定时器精度。

### 3.4 定时器层

```
Timer               — 单个定时任务
  └─ callback + expire 时间

TimerQueue          — 定时器队列
  ├─ 基于 timerfd_create(CLOCK_MONOTONIC)
  ├─ std::set<TimerPtr> 按到期时间排序
  ├─ timerfd 注册到 EventLoop 的 Channel
  ├─ 到期 → 读取 timerfd → 执行到期定时器回调
  └─ RunAfter / RunEvery

TimeWheel           — 时间轮（连接超时管理）
  ├─ _capacity 个槽（= timeout 秒），每个槽是 unordered_set<int>
  ├─ _index: 当前指针，每秒 +1
  ├─ 每秒 Tick(): 清空当前槽中所有 fd → 超时回调
  ├─ Insert(fd): 将 fd 插入当前位置前一格（最远端）
  ├─ Refresh(fd): 移除旧位置 → 重新插入远端（续期）
  ├─ Remove(fd): 从槽中删除 fd
  └─ 线程安全：所有方法持 mutex，Tick 持锁收集 fd 后释放锁再回调
```

**时间轮 vs TimerQueue 的区别：**

| | TimerQueue | TimeWheel |
|---|---|---|
| 用途 | 精确延时任务 | 连接超时批量管理 |
| 实现 | timerfd + set | 环形数组 + EventLoop::RunEvery(1) |
| 精度 | 毫秒级 | 秒级（每秒 Tick） |
| 操作复杂度 | O(log n) | O(1) — 插入/刷新/删除 |
| 线程 | 仅 EventLoop 所在线程 | 跨线程（mutex 保护） |

### 3.5 线程管理层

```
EventLoopThread     — 一个线程 + 一个 EventLoop
  ├─ ThreadFunc(): 创建 EventLoop → 条件变量通知主线程 → Loop() 阻塞
  ├─ StartLoop(): 创建线程，条件变量等待 EventLoop 就绪，返回 loop 指针
  └─ ~EventLoopThread(): Quit() + join()

EventLoopThreadPool — Worker 线程池
  ├─ _threads: vector<EventLoopThread>
  ├─ _loops: vector<EventLoop*> — 从各线程取出 loop 的缓存
  ├─ Start(): 创建 thread_num 个 EventLoopThread
  └─ GetNextLoop(): Round-Robin 轮询返回下一个 loop
```

### 3.6 连接层

```
TcpConnection : enable_shared_from_this<TcpConnection>
  ├─ _socket (unique_ptr<Socket>)   — socket 管理
  ├─ _channel (unique_ptr<Channel>) — 事件管理
  ├─ _in_buffer  (Buffer)           — 输入缓冲区
  ├─ _out_buffer (Buffer)           — 输出缓冲区
  ├─ _state: CONNECTING → CONNECTED → DISCONNECTING → DISCONNECTED
  │
  ├─ HandleRead():  非阻塞循环 recv → _in_buffer.Append → _message_cb
  ├─ HandleWrite(): send(_out_buffer.Peek) → _out_buffer.Retrieve → 无数据时 DisableWrite
  ├─ HandleClose(): _state = DISCONNECTED → _close_cb
  ├─ HandleError(): 直接触发 HandleClose()
  ├─ Send(str):     RunInLoop → SendInLoop → _out_buffer.Append + EnableWrite
  ├─ ConnectEstablished(): _state = CONNECTED + EnableRead + _connect_cb
  └─ ConnectDestroyed(): Channel::Remove + Socket::Close
```

**四种状态转换：**

```
CONNECTING ──[ConnectEstablished]──→ CONNECTED
                                         │
                                    [Shutdown]
                                         │
                                         ▼
                                   DISCONNECTING
                                         │
                                    [Send完毕]
                                         │
                                         ▼
CONNECTED ──[HandleClose/Error]──→ DISCONNECTED
```

### 3.7 连接管理（Acceptor + TcpServer）

```
Acceptor            — 监听器
  ├─ _listen_sock (Socket)    — 服务端监听 socket
  ├─ _channel (Channel)       — 监听 EPOLLIN 事件
  ├─ HandleRead(): accept → _new_connection_cb(new_fd)
  └─ Listen(): EnableRead

TcpServer           — 服务端总控
  ├─ _base_loop      — 主线程 EventLoop
  ├─ _thread_pool    — Worker 线程池
  ├─ _acceptor       — 监听器
  ├─ _time_wheel     — 连接超时管理
  ├─ _connections    — unordered_map<int, TcpConnection::Ptr>
  │
  ├─ AddConnection(fd):
  │     io_loop = thread_pool->GetNextLoop()  // Round-Robin
  │     conn = TcpConnection(io_loop, fd)
  │     设置回调
  │     _connections[fd] = conn
  │     time_wheel->Insert(fd)
  │     io_loop->RunInLoop → conn->ConnectEstablished()  // 由 worker 线程操作 epoll
  │
  ├─ RemoveConnection(conn):
  │     base_loop->RunInLoop → RemoveConnectionInLoop(conn)  // 回主线程处理
  │       → _time_wheel->Remove(fd)
  │       → _connections.erase(fd)
  │       → io_loop->RunInLoop → conn->ConnectDestroyed()    // 回 worker 线程释放
  │
  ├─ OnMessage(conn, buf):
  │     _time_wheel->Refresh(fd)     // 有消息就续期
  │     _message_cb(conn, buf)       // 调用业务层回调
  │
  └─ ~TcpServer():
        _time_wheel.reset()  // ① _alive=false，停止 Tick
        conns = copy(_connections)  // ② 拷贝连接列表
        _connections.clear()
        for conn: io_loop->RunInLoop → ConnectDestroyed()  // ③ 分发销毁
        // ④ thread_pool 析构 → Quit + join
```

**RemoveConnection 为什么需要"回主线程 → 再回 worker"的两次跳转？**

1. **Worker 线程**触发 `HandleClose` → `_close_cb` → `TcpServer::RemoveConnection`
2. RemoveConnection 需要操作 `_connections` map 和 `_time_wheel`——这些都是主线程资源
3. 因此 `RunInLoop` 到 **base_loop** → `RemoveConnectionInLoop`（完成 bookkeeping）
4. `ConnectDestroyed` 要操作 epoll（`epoll_ctl DEL`），必须在 **worker 线程**执行
5. 所以再 `RunInLoop` 回到 worker → `ConnectDestroyed`

这个两次跳转确保了：**属于谁的 epoll，就由谁操作。**

---

## 四、完整事件流

### 4.1 从 accept 到响应发送的完整链路

```
1. 客户端连接
   epoll_wait(listen_fd, EPOLLIN) → Acceptor::HandleRead()
     → accept(listen_fd) → new_fd
     → _new_connection_cb(new_fd)     // = TcpServer::AddConnection

2. 连接建立
   AddConnection(fd):
     io_loop = GetNextLoop()           // Round-Robin 选 worker
     conn = TcpConnection(io_loop, fd)
     设置 Message/Close 回调
     time_wheel->Insert(fd)            // 加入时间轮
     io_loop->RunInLoop → ConnectEstablished()
       → _state = CONNECTED
       → _channel->EnableRead()        // 注册 EPOLLIN（在 worker epoll）

3. 数据到达
   epoll_wait(conn_fd, EPOLLIN) → Channel::HandleEvent()
     → HandleRead()
       → while(recv > 0): _in_buffer.Append(data)
       → _message_cb(shared_from_this(), &_in_buffer)
         → TcpServer::OnMessage → time_wheel->Refresh(fd)
         → 业务层回调（Codec::OnMessage）

4. Codec 解码
   Codec::OnMessage(conn, buf):
     while (buf->ReadableSize() >= 6):
       Peek header → body_length + msg_type
       如果 buf 可读 < 6 + body_length → return（半包，等更多数据）
       Retrieve header + body
       Message::Create(type) → FromJson(body)
       _message_cb(conn, msg)           // → Dispatcher::Dispatch

5. 业务处理（Dispatcher 分发）
   Dispatch(conn, msg, ts):
     handler = _handlers[msg->GetType()]
     handler(conn, msg, ts)             // 例如 ChatServer::OnLogin

6. 异步 DB 查询
   ChatServer::OnLogin:
     user_service->Login(email, password, callback)
       → db->Execute(io_loop, DBTask, Callback)
         → DB worker 线程: pool.Borrow → 执行 SQL → pool.Return
         → Callback 投递回 io_loop

7. 发送响应
   业务回调中:
     LoginResponse resp;
     string encoded = codec.Encode(resp);   // header + JSON
     conn->Send(encoded);
       → RunInLoop → SendInLoop
         → _out_buffer.Append(str)
         → _channel->EnableWrite()          // 注册 EPOLLOUT

8. 数据发送
   epoll_wait(conn_fd, EPOLLOUT) → Channel::HandleEvent()
     → HandleWrite()
       → send(fd, _out_buffer.Peek(), readable)
       → _out_buffer.Retrieve(n)
       → if _out_buffer 空: _channel->DisableWrite()
```

### 4.2 连接关闭事件流

```
客户端断开连接:
  epoll_wait(conn_fd, EPOLLHUP) → Channel::HandleEvent()
    → _close_cb() → TcpConnection::HandleClose()
      → _state = DISCONNECTED
      → _close_cb(shared_from_this())      // = TcpServer::RemoveConnection

RemoveConnection(conn):                    // 可能在 worker 线程
  base_loop->RunInLoop → RemoveConnectionInLoop(conn)  // 回主线程
    → _time_wheel->Remove(fd)
    → _connections.erase(fd)
    → io_loop->RunInLoop → ConnectDestroyed()  // 回 worker 线程
      → _channel->Remove()                    // epoll_ctl DEL
      → _socket->Close()                      // close(fd)
```

### 4.3 超时断开事件流

```
每秒 Tick（主线程）:
  TimeWheel::Tick():
    _index = (_index + 1) % _capacity
    mutex lock:
      收集 _wheel[_index] 中所有 fd
      清除 _fd_slot_map 对应条目
      _wheel[_index].clear()
    mutex unlock:
      for each fd: _timeout_cb(fd)       // = TcpServer::HandleTimeout

TcpServer::HandleTimeout(fd):            // 在主线程
  _connections.find(fd) → conn
  RemoveConnectionInLoop(conn)            // 已在主线程，直接调用 InLoop 版本
    → _time_wheel->Remove(fd)
    → _connections.erase(fd)
    → io_loop->RunInLoop → ConnectDestroyed()
```

**TimeWheel 的线程安全设计要点：**

- `Tick()` 在**主线程**执行（通过 `EventLoop::RunEvery(1)` 注册到 base_loop）
- `Refresh()` 在 **worker 线程**执行（OnMessage 时调用）
- 因此 `Insert/Refresh/Remove/Tick` 都加 `mutex` 保护
- `Tick()` 采用 **持锁收集 + 释放锁后回调** 模式，避免死锁（回调中可能调用 Remove）

---

## 五、Buffer 设计

### 5.1 设计决策：线性 Buffer（非环形）

```
初始状态:
  [____________________________]   size = 1024
   ↑                            ↑
   _read_pos = 0                _write_pos = 0

写入 100 字节 "hello...":
  [hello...____________________]
   ↑        ↑
   _read    _write = 100

读出 50 字节后:
  [xxxxxxxxhello...____________]
            ↑        ↑
            _read=50 _write=100

可读数据 = _write_pos - _read_pos = 50
尾空闲 = BufferSize() - _write_pos
头空闲 = _read_pos

当尾空不够写入时:
  EnsureWritable(len):
    if 尾空 >= len → 直接写
    if 尾空 + 头空 >= len → MoveMem()
      memmove(buf.data(), buf.data()+_read_pos, readable)
      _read_pos = 0, _write_pos = readable
    else → ExpandMem(2x resize) + MoveMem()
```

**为什么选择线性 Buffer？**
- 实现简单，可读性好
- 小数据量下 memmove 开销可接受（通常几百字节）
- Muduo 使用环形 Buffer 是为了极致性能，但对于本项目规模，线性 Buffer 足够

### 5.2 关键方法

| 方法 | 说明 |
|------|------|
| `Append(str, len)` | 确保空间 → memcpy → 移动写指针 |
| `Peek()` | 返回可读数据的起始地址 |
| `Retrieve(len)` | 消费 len 字节 → 移动读指针 → 读写指针重合时重置 |
| `ReadableSize()` | `_write_pos - _read_pos` |
| `RetrieveAllAsString()` | 取出全部可读数据并清空 |

---

## 六、关键设计决策

### 6.1 为什么 TcpConnection 继承 enable_shared_from_this？

问题场景：`HandleRead` 中需要把 `this` 传给 `_message_cb`，但 `HandleRead` 是由 Channel 的回调触发的，Channel 只持有裸 `this` 指针。

```cpp
// TcpConnection::HandleRead 中:
if(_message_cb){
    _message_cb(shared_from_this(), &_in_buffer);  // 安全地传递 shared_ptr
}
```

如果不传 `shared_ptr`，而是传裸 `this`：
- 上层回调如果持有这个指针，可能在 TcpConnection 析构后悬空
- 上层无法参与 TcpConnection 的生命周期管理

### 6.2 为什么 Send 需要通过 RunInLoop 分发？

```cpp
void TcpConnection::Send(const std::string &str){
    _loop->RunInLoop([conn = shared_from_this(), str]{
        conn->SendInLoop(str);
    });
}
```

- `SendInLoop` 内部操作 `_out_buffer`（非线程安全）+ `_channel->EnableWrite()`（操作 epoll）
- 必须保证在 io_loop 线程执行
- Lambda 捕获 `shared_from_this()` 而不是裸 `this`，防止 Send 任务还在队列中时 TcpConnection 被析构

### 6.3 为什么 DoPendingFunctors 用 swap 而不是直接遍历？

```cpp
void EventLoop::DoPendingFunctors(){
    FunctorList tasks;
    {
        std::unique_lock<std::mutex> lock(_mutex);
        tasks.swap(_pending_functors);  // 交换，不拷贝
    }
    // 已释放锁，安全执行
    for(auto &f : tasks) f();
}
```

- 减少临界区时间：swap 是 O(1) 操作
- 避免在持锁情况下执行回调（回调可能再次调用 QueueInLoop，造成死锁）
- 不会漏掉新任务：执行期间新投递的任务会进入新的 _pending_functors

### 6.4 为什么 TimeWheel 析构使用 shared_ptr\<bool\> _alive？

```cpp
// TimeWheel 构造:
auto alive = _alive;
_loop->RunEvery(1, [this, alive]{
    if (*alive) Tick();
});

// TimeWheel 析构:
*_alive = false;  // 阻止后续 Tick
```

- TimeWheel 通过 `RunEvery` 向 EventLoop 注册了周期性回调
- TimeWheel 析构后，EventLoop 中可能还有尚未执行的 `RunEvery` 回调
- 如果 TimeWheel 已析构但回调仍访问 `this->_wheel` → use-after-free
- `shared_ptr<bool>` 让回调共享一个生命周期标志，TimeWheel 析构时置 `false`

### 6.5 为什么 TcpServer 析构要多步操作？

```cpp
~TcpServer(){
    _time_wheel.reset();            // ① 停止 Tick（_alive=false）
    vector<TcpConnection::Ptr> conns;
    for(auto &p : _connections)     // ② 拷贝连接列表
        conns.push_back(p.second);
    _connections.clear();           // ③ 清空 map
    for(auto &conn : conns)         // ④ 分发销毁到各 worker
        conn->GetLoop()->RunInLoop → ConnectDestroyed();
    // ⑤ thread_pool 析构：Quit + join（确保 ④ 的任务执行完）
}
```

- 析构顺序至关重要：先停 TimeWheel，再清连接，最后停线程池
- 线程池最后析构确保 `ConnectDestroyed` 投递的任务在 worker 退出前执行完毕

---

## 七、文件组织结构

```
MyMuduo/
├── include/
│   ├── base/          — 基础工具 (NoCopy, Exception, Timestamp, Crypto)
│   ├── net/           — 网络框架头文件
│   │   ├── Buffer.h, Channel.h, Socket.h, InetAddress.h
│   │   ├── Poller.h, EpollPoller.h, EventLoop.h
│   │   ├── Timer.h, TimerQueue.h, TimeWheel.h
│   │   ├── EventLoopThread.h, EventLoopThreadPool.h
│   │   ├── TcpConnection.h, Acceptor.h, TcpServer.h, Connector.h
│   │   └── Log.h
│   ├── proto/         — 协议层
│   │   ├── Protocol.h, MessageType.h, Message.h
│   │   ├── Codec.h, Dispatcher.h
│   ├── db/            — 数据库层
│   │   ├── ConnectionPool.h, Database.h
│   │   ├── UserDao.h, FriendDao.h, PrivateMessageDao.h
│   ├── cache/         — Redis 缓存层
│   │   ├── RedisPool.h, RedisCache.h, RedisDao.h
│   └── service/       — 业务服务层
│       ├── ChatServer.h, UserService.h, FriendService.h, EmailSender.h
├── src/               — 实现文件（与 include 对应）
├── test/              — 单元测试（Google Test）
├── examples/          — 使用示例
├── static/            — 前端静态页面
├── third_party/       — 第三方库
└── CMakeLists.txt
```

---

## 八、技术栈总结

| 层面 | 技术 |
|------|------|
| 语言 | C++17 |
| I/O 模型 | epoll (ET 模式，非阻塞) |
| 线程模型 | One Loop Per Thread |
| 跨线程唤醒 | eventfd |
| 定时器 | timerfd (毫秒级) + 时间轮 (秒级) |
| 序列化 | jsoncpp (JSON) |
| 数据库 | MySQL (MySQL Connector/C++) |
| 缓存 | Redis (hiredis) |
| 构建 | CMake |
| 测试 | Google Test |
| 配置 | SimpleIni |
| HTTP | cpp-httplib (第三方) |

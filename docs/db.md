# MyMuduo 数据库 & Redis 缓存层说明

> 本文档描述 MyMuduo 的 MySQL 持久化存储和 Redis 缓存层的设计与实现。

---

## 一、整体架构

```
┌──────────────────────────────────────────┐
│              业务服务层                    │
│  UserService / FriendService             │
│      │              │                    │
│      ├──────────────┤                    │
│      ▼              ▼                    │
│  Database        RedisCache              │
│  (异步执行)      (异步执行)               │
│      │              │                    │
│      ▼              ▼                    │
│  ConnectionPool  RedisPool               │
│  (连接池)        (连接池)                │
│      │              │                    │
│      ▼              ▼                    │
│   MySQL           Redis                  │
└──────────────────────────────────────────┘
```

**设计原则：**
- **异步执行**：所有 DB/Redis 操作在独立 worker 线程执行，不阻塞 IO 线程
- **回调回归**：结果通过 `QueueInLoop` 回到 IO 线程，保证线程安全
- **连接池复用**：避免频繁创建/销毁连接
- **Cache-Aside**：Redis 作为 MySQL 的缓存加速层，miss 时回退 MySQL

---

## 二、MySQL 连接池 (ConnectionPool)

### 2.1 设计

```
ConnectionPool
  ├─ _pool: std::queue<sql::Connection*>  空闲连接队列
  ├─ _active_count: std::atomic<int>      总连接数（空闲 + 借出）
  ├─ _max_size: int                       最大连接数
  ├─ _mutex + _cv                         线程同步
  │
  ├─ Borrow(timeout_ms) → sql::Connection*   借用连接
  ├─ Return(conn)                       归还连接
  ├─ Remove(conn)                       剔除坏连接
  ├─ Close()                            关闭连接池
  └─ ~ConnectionPool()                  析构：关闭所有连接
```

### 2.2 Borrow 流程

```
Borrow(timeout_ms):
  while (!_closed):
    ① 空闲队列有连接？
      → pop → CheckHealth (SELECT 1)
      → 健康 → 返回
      → 不健康 → delete → active_count-- → continue

    ② 未达上限 (active_count < max_size)？
      → CreateConnection()（MySQL Connector/C++）
      → active_count++
      → 返回

    ③ 已达上限 + 有超时？
      → cv.wait_until(lock, deadline)  // 阻塞等待归还
      → 超时 → 返回 nullptr

    ④ 已达上限 + 无超时？
      → cv.wait(lock)                   // 无限等待
```

**设计要点：**

| 要点 | 实现 |
|------|------|
| **懒初始化** | 连接按需创建，不是一把全建好 |
| **健康检查** | Borrow 时执行 `SELECT 1`，坏连接自动丢弃并重新创建 |
| **阻塞等待** | 连接全忙时阻塞，支持超时 |
| **线程安全** | mutex + condition_variable |

### 2.3 Return 流程

```cpp
void ConnectionPool::Return(sql::Connection* conn) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (conn->isClosed()) {
        delete conn;      // 连接已关闭 → 销毁
        _active_count--;
    } else {
        _pool.push(conn); // 放回空闲队列
    }
    _cv.notify_one();     // 唤醒一个等待 Borrow 的线程
}
```

### 2.4 Remove 流程

```cpp
void ConnectionPool::Remove(sql::Connection* conn) {
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active_count--;
    }
    delete conn;          // 销毁坏连接
    _cv.notify_one();     // 通知可创建新连接
}
```

**Remove vs Return 的区别：**
- `Return`：连接正常，放回池中复用
- `Remove`：连接异常（SQL 执行抛出异常），直接销毁，下一次 `Borrow` 会创建新连接

---

## 三、异步数据库执行器 (Database)

### 3.1 生产者-消费者模型

```
主线程 / IO 线程 (Producer)          DB Worker 线程 (Consumer)
─────────────────────────           ──────────────────────

db->Execute(loop, fn, callback)     WorkerThread():
  │                                    while(_running):
  ├─ lock _task_mutex                  ├─ cv.wait(_task_mutex)
  ├─ _tasks.push({loop,fn,cb})         ├─ task = _tasks.pop()
  ├─ unlock                            ├─ unlock
  └─ cv.notify_one() ─────────────→    ├─ conn = pool.Borrow()
                                       ├─ try: task.fn(conn)     ← 执行 SQL
                                       ├─ catch: pool.Remove()   ← 坏连接
                                       ├─ else: pool.Return()    ← 正常
                                       └─ loop->QueueInLoop(cb)  ← 回调回 IO 线程
```

### 3.2 核心实现

```cpp
class Database : NoCopy {
public:
    using DBTask = std::function<void(sql::Connection*)>;
    using Callback = std::function<void()>;

    // 异步执行
    void Execute(EventLoop* loop, DBTask fn, Callback callback);

private:
    ConnectionPool _pool;                   // 连接池
    std::queue<Task> _tasks;                // 任务队列
    std::mutex _task_mutex;
    std::condition_variable _task_cv;
    std::vector<std::thread> _workers;       // DB worker 线程组
    std::atomic<bool> _running{true};
};
```

### 3.3 连接异常重试

```cpp
// WorkerThread 中:
sql::Connection* conn = nullptr;
int retries = 3;
while (retries-- > 0 && _running) {
    conn = _pool.Borrow(5000);
    if (conn) break;
}
```

借连接时支持 3 次重试，应对临时的连接耗尽。

### 3.4 结果回调回到 IO 线程

```cpp
// 执行完 DBTask 后：
if (task.callback) {
    if (task.loop) {
        task.loop->QueueInLoop(std::move(task.callback));  // 回到 IO 线程
    } else {
        task.callback();  // 同步模式：debug 用
    }
}
```

**为什么必须回到 IO 线程？**
业务回调通常需要操作 TcpConnection（如 `conn->Send(response)`），这必须在 conn 所属的 IO 线程执行。

---

## 四、数据访问对象 (DAO)

### 4.1 UserDao — 用户数据

```
UserDao (纯静态方法)
  ├─ InsertUser(conn, email, hash, salt, name, out_id)    — 注册
  ├─ GetUserByEmail(conn, email, out_user)                 — 登录查询
  ├─ GetUserById(conn, id, out_user)
  ├─ EmailExists(conn, email)                              — 注册查重
  ├─ UpdateAvatar(conn, user_id, path)
  ├─ GetUserProfile(conn, user_id, out)                    — 公开档案
  ├─ UpdateProfile(conn, user_id, ...)                     — 编辑个人资料
  ├─ DeleteUser(conn, user_id)                             — 注销账号（级联）
  ├─ InsertVerificationCode(conn, email, code, type, expire)— 存验证码
  └─ VerifyCode(conn, email, code, type)                   — 校验验证码
```

**安全设计：**
- 所有 SQL 使用 **Prepared Statement**，防 SQL 注入
- 密码存储格式：`SHA256(password + salt)` → hex 字符串
- 验证码有过期时间，验证后标记已使用

### 4.2 FriendDao — 好友数据

```
FriendDao (纯静态方法)
  ├─ SearchUserByEmail(conn, keyword, exclude_id, out)
  ├─ SendFriendRequest(conn, from_id, to_id, out_req_id, out_auto)
  ├─ AcceptFriendRequest(conn, user_id, request_id, out_friend_id)
  ├─ RejectFriendRequest(conn, user_id, request_id)
  ├─ DeleteFriend(conn, user_id, friend_id)
  ├─ GetFriendList(conn, user_id, out)
  ├─ GetPendingRequests(conn, user_id, out)
  ├─ IsFriend(conn, user_id, friend_id)
  ├─ HasPendingRequest(conn, user_id, other_id)
  ├─ SetRemark(conn, user_id, friend_id, remark)
  └─ GetFriendDetail(conn, user_id, friend_id, out)
```

**好友关系模型（双向存储）：**

```
A 向 B 发请求:
  INSERT (user_id=A, friend_id=B, status=0)  — 待处理

B 同意:
  UPDATE 上条记录 status=1                    — A→B 已接受
  INSERT (user_id=B, friend_id=A, status=1)  — B→A 已接受（双向）

任一方删除:
  DELETE 两条记录（双向删除）
```

**特殊设计：反向请求自动接受**

```cpp
SendFriendRequest(A, B):
  检查是否 B 已经向 A 发了请求（status=0）?
    → 是：自动接受 → 双方成为好友（免去 B 再操作的步骤）
    → 否：正常插入，等待 B 处理
```

### 4.3 PrivateMessageDao — 私聊消息

```
PrivateMessageDao (纯静态方法)
  ├─ SendMessage(conn, from, to, content, out_id, reply_to)
  ├─ GetConversation(conn, user_id, friend_id, after_id, limit, out, updates)
  ├─ RevokeMessage(conn, msg_id, user_id)    — 2分钟内可撤回
  ├─ MarkAsRead(conn, user_id, friend_id)    — 标记已读
  ├─ GetTotalUnreadCount(conn, user_id)       — 总未读数
  └─ GetUnreadCounts(conn, user_id)           — 按好友分组的未读数
```

**支持的功能：**
- 增量拉取（`after_id`：只拉新消息）
- 消息更新拉取（`updated_since`：拉取已撤回/已编辑的消息）
- 回复引用（`reply_to_id` + `reply_preview`）
- 2 分钟内撤回（仅发送者）

---

## 五、Redis 连接池 (RedisPool)

### 5.1 设计

与 MySQL 连接池同构的设计，但操作的是 hiredis 的 `redisContext*`：

```cpp
class RedisPool : NoCopy {
public:
    explicit RedisPool(const std::string& host, int port, int max_size = 8);

    redisContext* Borrow(int timeout_ms = 5000);
    void Return(redisContext* ctx);
    void Remove(redisContext* ctx);
    void Close();

private:
    std::queue<redisContext*> _pool;      // hiredis 连接队列
    std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<int> _active_count;
    std::atomic<bool> _closed;
};
```

**健康检查：** Borrow 时发送 `PING` 命令：

```cpp
bool RedisPool::CheckHealth(redisContext* ctx) {
    if (!ctx || ctx->err) return false;
    redisReply* reply = (redisReply*)redisCommand(ctx, "PING");
    bool ok = (reply && reply->type == REDIS_REPLY_STATUS &&
               std::string(reply->str) == "PONG");
    if (reply) freeReplyObject(reply);
    return ok;
}
```

---

## 六、异步 Redis 执行器 (RedisCache)

### 6.1 设计

与 `Database` 完全同构的异步执行模型：

```cpp
class RedisCache : NoCopy {
public:
    using RedisTask = std::function<void(redisContext*)>;
    using Callback = std::function<void()>;

    void Execute(EventLoop* loop, RedisTask fn, Callback callback);

private:
    RedisPool _pool;
    std::queue<Task> _tasks;
    std::mutex _task_mutex;
    std::condition_variable _task_cv;
    std::vector<std::thread> _workers;
    std::atomic<bool> _running{true};
};
```

调用示例（UserService 中）：

```cpp
_redis->Execute(_loop,
    // RedisTask: 在 Redis worker 线程执行
    [email, type, code, expire](redisContext* ctx) {
        RedisDao::SetVerificationCode(ctx, email, type, code, expire);
    },
    // Callback: 回到 IO 线程执行
    [this, email]() {
        TrySendEmail(_email_cfg, email, code);
    });
```

---

## 七、Redis 数据访问对象 (RedisDao)

### 7.1 Key 命名规范

| 数据类型 | Key 格式 | 示例 | TTL |
|---------|---------|------|-----|
| 用户信息 Hash | `user:info:<id>` | `user:info:42` | 1 小时 |
| 邮箱映射 String | `user:email:<email>` | `user:email:alice@t.com` | 1 小时 |
| 验证码 String | `code:<type>:<email>` | `code:1:alice@t.com` | 5 分钟 |
| 在线状态 String | `online:<user_id>` | `online:42` | 70 秒（超时 2 倍） |
| 好友列表 Set | `friend:list:<user_id>` | `friend:list:42` | 5 分钟 |
| 搜索历史 List | `search:history:<user_id>` | `search:history:42` | 永久（最多 6 条） |

### 7.2 功能清单

```
RedisDao (纯静态方法)

用户信息缓存 (Hash):
  ├─ CacheUserInfo(ctx, user)       — 写入 Hash
  ├─ GetCachedUserInfo(ctx, id, out) — 读取 Hash
  └─ InvalidateUserCache(ctx, id)    — 删除 Hash (DELETE key)

邮箱映射 (String):
  ├─ CacheEmailMapping(ctx, email, id)    — SET user:email:<email> = id
  ├─ GetUserIdByEmail(ctx, email, out_id) — GET 邮箱 → ID
  └─ InvalidateEmailMapping(ctx, email)   — DEL key

验证码 (String + TTL):
  ├─ SetVerificationCode(ctx, email, type, code, expire) — SETEX
  └─ GetAndConsumeVerificationCode(ctx, email, type, out) — GET + DEL

在线状态 (String + TTL):
  ├─ SetUserOnline(ctx, user_id, ttl)      — SETEX online:xx 1
  ├─ IsUserOnline(ctx, user_id)            — EXISTS
  └─ SetUserOffline(ctx, user_id)          — DEL

好友列表 (Set):
  ├─ GetFriendIdSet(ctx, user_id, out)     — SMEMBERS
  ├─ CacheFriendIdSet(ctx, user_id, ids, ttl) — SADD + EXPIRE
  ├─ AddFriendToSet(ctx, user_id, friend_id)  — SADD
  ├─ RemoveFriendFromSet(ctx, user_id, friend_id) — SREM
  └─ IsInFriendSet(ctx, user_id, friend_id)    — SISMEMBER

搜索历史 (List):
  ├─ AddSearchHistory(ctx, user_id, keyword) — LPUSH + LTRIM 0 5
  └─ GetSearchHistory(ctx, user_id, out)     — LRANGE 0 5
```

### 7.3 邮箱映射的实现

```
为什么需要邮箱 → ID 映射？

场景：用户 alice@t.com 登录
  1. 前端传来 email + password
  2. 需要查用户 ID
  3. 有 Redis 时：
     GET user:email:alice@t.com → 42
     HGETALL user:info:42 → {id, username, password, salt, ...}
     （两次 Redis 操作，~1ms）
  4. 无 Redis 时：
     SELECT id, password, salt, ... FROM users WHERE email = 'alice@t.com'
     （一次 MySQL 查询，~5-10ms）
```

---

## 八、Cache-Aside 模式

### 8.1 读流程（以登录为例）

```
Login(email, password):
  ① if _redis:
       RedisDao::GetUserIdByEmail(email) → user_id
       if 命中:
         RedisDao::GetCachedUserInfo(user_id) → UserInfo
         if 命中:
           验密码 → callback(成功/失败)
           return

  ② LoginViaMySQL(email, password):
       UserDao::GetUserByEmail(email) → UserInfo
       验密码

       // 回填 Redis
       if _redis:
         RedisDao::CacheEmailMapping(email, user_id)
         RedisDao::CacheUserInfo(user_id, info)

       callback(成功/失败)
```

**为什么 miss 后才回填？**
- 热点用户自然会被缓存
- 冷门用户不浪费 Redis 内存
- TTL 自动淘汰，自然过期

### 8.2 写流程（以注册为例）

```
Register(email, password, username):
  ① MySQL 操作:
     UserDao::InsertUser(email, hash, salt, username) → user_id

  ② 失效 Redis (如果有):
     // 注册时 Redis 中还没有此用户数据，不操作也可以
     RedisDao::CacheEmailMapping(email, user_id)  // 直接写入

  ③ callback(成功, user_id)
```

**为什么是 Delete（失效）而非 Update？**

| 策略 | 并发安全 | 实现复杂度 |
|------|---------|-----------|
| Update（更新缓存） | 需要分布式锁，否则并发写导致不一致 | 高 |
| Delete（删除缓存） | 最终一致性，下次读时重建 | 低 |

---

## 九、异步执行线程模型

```
┌───────────────────────────────────────┐
│               main thread             │
│  TcpServer + Acceptor + TimeWheel     │
└───────────────────────────────────────┘

┌──────────┐ ┌──────────┐               ┌──────────┐ ┌──────────┐
│ IO Thread│ │ IO Thread│               │DB Worker │ │DB Worker │
│ (epoll)  │ │ (epoll)  │               │ (MySQL)  │ │ (MySQL)  │
│          │ │          │               │          │ │          │
│ 业务处理 │ │ 业务处理 │               │ sql::    │ │ sql::    │
│ Dispatcher│ │Dispatcher│              │ Conn     │ │ Conn     │
└────┬─────┘ └────┬─────┘               └────┬─────┘ └────┬─────┘
     │            │                          │            │
     └────────────┼──────────────────────────┘            │
                  │  db->Execute(loop, task, callback)     │
                  │  redis->Execute(loop, task, callback)  │
                  └───────────────────────────────────────→│
                                                           │
┌──────────┐ ┌──────────┐                               │
│  Redis   │ │  Redis   │ ←── 独立的 Redis worker 线程组
│  Worker  │ │  Worker  │
│ hiredis  │ │ hiredis  │
└──────────┘ └──────────┘
```

**为什么 DB 和 Redis 有各自的 worker 线程组？**
- DB 操作可能较长（几十 ms），Redis 极快（<1ms）
- 分开避免 Redis 请求被 DB 操作阻塞
- 各自独立扩展

---

## 十、安全设计

### 10.1 防 SQL 注入

所有 SQL 使用 **Prepared Statement**：

```cpp
// ✅ 安全
std::unique_ptr<sql::PreparedStatement> pstmt(
    conn->prepareStatement("SELECT * FROM users WHERE email = ?"));
pstmt->setString(1, email);

// ❌ 危险（项目中不使用）
std::string sql = "SELECT * FROM users WHERE email = '" + email + "'";
```

### 10.2 密码存储

```
密码处理流程:
  raw_password → SHA256(raw_password + random_salt) → hex string

存储:
  users.password = "a3f2b8c9..."  (64 char hex)
  users.salt     = "d7e1f4a2..."  (64 char hex)
```

- 每用户独立随机盐（注册时生成 32 字节随机数）
- SHA256 通过 OpenSSL 实现（`Crypto::SHA256`）
- 不使用快速哈希（MD5/SHA1），但这些也不够安全。生产环境应使用 bcrypt/argon2

### 10.3 密码传输

当前设计：前端明文传输密码到服务器（服务器端哈希）。

实际生产环境应使用 HTTPS（TLS 加密），在传输层加密而非应用层。本项目的 Web 前端通过 HTTP 与后端通信，Demo 性质下可接受。

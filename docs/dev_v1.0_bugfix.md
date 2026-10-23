# MyMuduo 开发文档 · v1.0 BUG 修复

| 项 | 值 |
|---|---|
| 版本 | v1.0-bugfix |
| 基线 | `88780b7` + 当前未提交重构（RedisPool→redis++、main 拆分 HttpServer） |
| 日期 | 2026-10-23 |
| 范围 | 只修正确性与安全问题，不引入新功能 |
| 进度 | **4 / 10** |

## 0. 版本约定

- 一个版本一个文档：`docs/dev_v1.0_bugfix.md`、`docs/dev_v1.1_xxx.md`。
- 每版只写增量；上一版未完成项原样带入并保留编号；已完成项只在「版本记录」留一行，不复制细节。
- v1.0 全部勾选后，复制本文件为 v1.1，保留未完成项，新增 v1.1 条目；不在原文件继续加需求。
- 每条格式固定：**现象 / 发现 / 根因 / 修复 / 验证**。
- 完成后把 `- [ ]` 改成 `- [x]`，并在行尾补 `（完成：YYYY-MM-DD）`。
- 完成定义：构建通过 + 新增回归测试通过 + 无新增 ASan/TSan 报错 + 文档已勾选。

## 1. 本版目标

让 TCP 连接在正常关闭、RST、半关闭下不泄漏、不丢包、不空转；让进程能优雅退出；堵住 HTTP 越权与验证码回显；把构建、测试、文档拉回可用状态。

## 2. BUG 清单

### P0 · 连接与生命周期

- [x] **B01 连接关闭回调被覆盖**（完成：2026-10-23）
  - 现象：TCP 客户端断开后 `_connections` 与 fd 不立即释放，要等约 60s 时间轮超时；RST 时可能反复触发 HUP。
  - 发现：对照 `TcpServer::AddConnection`（`src/net/TcpServer.cpp:58`）与 `ConnectEstablished` 的回调顺序，发现 `ChatServer::OnConnection`（`src/service/ChatServer.cpp:229`）二次设置了 close 回调。用 `nc` 连上再断开，观察 `/proc/<pid>/fd` 数量不回落。
  - 根因：`TcpConnection` 只有一个 `_close_cb`；业务层覆盖了 `TcpServer::RemoveConnection`，正常关闭路径不再做 map/时间轮清理。
  - 修复：`TcpServer` 新增 `SetCloseNotifyCallback`，在 `RemoveConnectionInLoop` 中通知业务；业务只清在线状态，不再碰 close_cb。最小改动可保存旧回调并链式调用。
  - 验证：新增 `TcpLifecycleTest.ConnectThenCloseReleasesConnection`：连接→关闭→2s 内连接数归零、fd 数回落；RST 场景 CPU 不升高。

- [x] **B02 ET 下 accept 只取一次，且把 0 当合法 fd**（完成：2026-10-23）
  - 现象：并发建连时部分连接迟迟不被 accept；极端情况下 fd 0 被当成连接。
  - 发现：`Channel::EnableRead` 设置了 `EPOLLET`（`src/net/Channel.cpp:25`），但 `Acceptor::HandleRead` 只 accept 一次；对照 Muduo 的 while-accept 写法。另查 `Socket::Accept` 在 EAGAIN 时返回 0（`src/net/Socket.cpp:41`）。
  - 根因：ET 下必须一次把 accept 队列排空；`0` 与 `-1` 语义混用，`connfd < 0` 漏判 0。
  - 修复：`HandleRead` 循环 accept 到 EAGAIN；`Socket::Accept` 在 EAGAIN 时统一返回 `-1`；`Acceptor` 以 `< 0` 结束循环；`BACK_LOG` 10→128 以承受突发建连。
  - 验证：新增 `TcpLifecycleTest.BurstConnectionsAreAllAccepted`：并发 100 连接全部 accepted；fd 0 不进入 epoll。

- [x] **B03 ET 下 `HandleWrite` 只 send 一次，部分写会卡住**（完成：2026-10-23）
  - 现象：大响应或慢客户端时，`_out_buffer` 还有数据但不再发送，连接像卡住。
  - 发现：ET 语义 + `TcpConnection::HandleWrite` 只调用一次 `send`（`src/net/TcpConnection.cpp:56`）；用小 `SO_SNDBUF` + 不读的客户端可复现。
  - 根因：一次 `send` 可能只写一部分；socket 仍可写但 ET 不会再次通知 EPOLLOUT。
  - 修复：`while` 循环 send 直到 EAGAIN 或 buffer 空，再 `DisableWrite`。
  - 验证：新增 `TcpLifecycleTest.PartialWriteDrainsOutBuffer`：发送 8MB 响应，客户端延迟读取，最终字节数一致。

- [x] **B04 半关闭丢最后一批数据**（完成：2026-10-23）
  - 现象：客户端 `send` 后立即 `close`，服务端可能收不到最后一条消息。
  - 发现：读 `HandleRead` 的 `n==0` 分支（`src/net/TcpConnection.cpp:13-15`），发现它在 `_message_cb` 之前 return。
  - 根因：半关闭时先把连接关掉，`_in_buffer` 里已收到的数据没有分发。
  - 修复：读到 0 先 break，分发 buffer，再 `HandleClose()`；或用 `pending_close` 标记延迟关闭。
  - 验证：新增 `TcpLifecycleTest.SendThenHalfCloseDeliversBufferedData`：客户端发 64KB 后 `shutdown(SHUT_WR)`，服务端 handler 收到后才关闭。

- [ ] **B05 HTTP 服务无法优雅退出**
  - 现象：Ctrl-C / SIGTERM 后进程不退出，卡在 `http_thread.join()`。
  - 发现：走查 `examples/chat_server/main.cpp:55-57` 与 `RunHttpServer` 的 `http.listen`（`examples/chat_server/HttpServer.cpp:1259`），没有 `stop()`；实际 Ctrl-C 复现。
  - 根因：信号处理只 `Quit()` 了 base_loop；httplib 的 listen 仍阻塞，join 永远等不到。
  - 修复：全局保存 `httplib::Server*`；main 在 join 前调用 `stop()`（不要在信号处理函数里直接 stop）；或信号线程置标志，主线程随后 stop。
  - 验证：SIGTERM 后 2s 内退出，8080/8888 端口释放。

### P1 · 安全与存储

- [ ] **B06 HTTP 无鉴权，user_id 全信客户端**
  - 现象：知道 user_id 就能读任意对话、删任意账号；send-code 直接把验证码返回给前端。
  - 发现：逐个检查 HTTP handler 的参数来源，`/api/user/account/delete`、`/api/messages/conversation`、`/api/send-code` 都只信请求体/query；`HttpServer.cpp:186` 回显 code。
  - 根因：没有会话/令牌，服务端不校验“你是谁”；TCP 的 `OnLogout` 也直接使用 `req->user_id`（`src/service/ChatServer.cpp:373`）。
  - 修复：登录签发 token（优先 HttpOnly Cookie，或 Bearer），加认证中间件解析 uid；所有接口用 uid；`send-code` 不回显；加简单限流。TCP 侧用连接绑定的 uid，不读请求体。
  - 验证：无 token 返回 401；A 的 token 访问 B 的数据返回 403；send-code 响应无 code。

- [ ] **B07 HTTP 把异步 DB 又做回了同步阻塞**
  - 现象：DB 慢/断连时 HTTP 接口整体变慢；连接池耗尽后请求最多阻塞 15s。
  - 发现：`RunDBSync`（`examples/chat_server/HttpUtils.cpp:17`）用条件变量同步等待；`ConnectionPool::Borrow` 在持锁状态下 `CreateConnection()`（`src/db/ConnectionPool.cpp:85`）。
  - 根因：HTTP 层把异步 DB 封装成同步；建连是网络 IO，放在锁内会串行化所有 Borrow。
  - 修复（本版最小）：建连移到锁外（预留名额→锁外建连→回填/回滚）；Borrow 等待加超时；DB 不可用时 HTTP 返回 503。完整异步化放 v1.1。
  - 验证：停 MySQL 后接口 5s 内返回 503，不拖垮 `/health`；并发 50 请求无长时间排队。

- [ ] **B08 HTTP 侧 Redis 每请求新建且硬编码**
  - 现象：改 `config.ini` 的 Redis 地址不生效；Redis 挂掉时部分 HTTP 接口抛异常。
  - 发现：全局搜 `sw::redis::Redis rctx`，`HttpServer.cpp` 有 7 处硬编码 `127.0.0.1:6379`。
  - 根因：HTTP 侧绕过 `RedisCache`，每个请求新建客户端，地址写死，且无异常处理。
  - 修复：`AppConfig` 增加 Redis 配置；`RunHttpServer` 接收连接参数或共享客户端；统一复用 `RedisDao`；加 try/catch 降级。
  - 验证：改端口后生效；停 Redis 后 ping 返回 `success=false` 而非 500。

### P2 · 收尾

- [ ] **B09 生命周期与边界小缺陷**
  - 现象：`EventLoop` 泄漏；同账号重复登录后在线状态错乱；`timeout=0` 启动崩溃。
  - 发现：代码审查发现 `ChatServer.cpp:16` 裸 `new EventLoop` 无 delete；两次登录覆盖 `_online_users[user_id]`，旧连接关闭按 user_id 删除；`TimeWheel` 构造未校验 capacity。
  - 根因：所有权不清晰；在线映射以 user_id 为键，没有“连接代际”校验；边界值未防御。
  - 修复：`unique_ptr<EventLoop>`；登录时先解绑/关闭旧连接，close 回调校验 `conn` 是否为当前映射；`TimeWheel` 校验 `timeout > 0`。
  - 验证：重复登录两次，旧连接关闭不影响新连接在线状态；`timeout=0` 报错退出；ASan 无泄漏。

- [ ] **B10 测试接入 CTest + README 纠偏**
  - 现象：`ctest` 显示 0 个测试；README 的 `sql/init.sql`、`config.ini.example`、`./test_proto` 36 用例与仓库不符。
  - 发现：`ctest -N`；逐条对照 README 与实际目录。
  - 根因：CMake 未 `enable_testing()/add_test`；文档未随重构更新。
  - 修复：加 `enable_testing()` + `add_test`（test_proto、test_tcp_integration、test_http_auth；依赖 MySQL/Redis 的用 label 标 optional）；README 修正路径、测试数、Redis++、HTTP 8080、TCP 实验状态。
  - 验证：`ctest --output-on-failure` 全绿；按 README 从零构建可跑。

## 3. 修复顺序

1. B01–B04：网络层一起改，跑新的 TCP 集成测试（建议分支 `fix/tcp-lifecycle`）。
2. B05：进程退出路径。
3. B06：鉴权，独立分支 `fix/http-auth`，方便回滚。
4. B07 / B08：存储与配置。
5. B09 / B10：收尾、补测试、同步文档。

## 4. 完成验收

- [ ] `cmake --build build -j` 全绿。
- [ ] `ctest --output-on-failure` 全绿。
- [ ] 手工验收：Ctrl-C 2s 退出；100 并发 connect 全接受；slow reader 收全 1MB；send+close 不丢帧。
- [ ] ASan/TSan 无新增报错。
- [ ] README 与本文件同步勾选。

## 5. 不在本版范围

- WebSocket、HTTP 与 TCP 共用 Service 层。
- 群聊、消息落库、离线消息。
- 环形 Buffer、writev、对象池等性能优化。
- 头像上传类型/大小校验（并入 v1.1 安全项）。
- 多进程 / 分布式。

## 6. 版本记录

| 版本 | 日期 | 主题 | 状态 |
|---|---|---|---|
| v1.0 | 2026-10-23 | BUG 修复（本文件） | 进行中 4/10 |
| v1.1 | 待定 | 业务统一 + WebSocket + 群聊 | 未开始 |

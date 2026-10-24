# MyMuduo 开发文档 · v1.0 BUG 修复

| 项 | 值 |
|---|---|
| 版本 | v1.0-bugfix |
| 基线 | `88780b7` + 当前未提交重构（RedisPool→redis++、main 拆分 HttpServer） |
| 日期 | 2026-10-23 |
| 范围 | 只修正确性与安全问题，不引入新功能 |
| 进度 | **7 / 8（主线；B07/B08 暂缓）** |

## 0. 版本约定

- 一个版本一个文档：`docs/dev_v1.0_bugfix.md`、`docs/dev_v1.1_xxx.md`。
- 每版只写增量；上一版未完成项原样带入并保留编号；已完成项只在「版本记录」留一行，不复制细节。
- v1.0 全部勾选后，复制本文件为 v1.1，保留未完成项，新增 v1.1 条目；不在原文件继续加需求。
- 每条格式固定：**现象 / 发现 / 根因 / 修复 / 验证**。
- 完成后把 `- [ ]` 改成 `- [x]`，并在行尾补 `（完成：YYYY-MM-DD）`。
- 完成定义：构建通过 + 新增回归测试通过 + 无新增 ASan/TSan 报错 + 文档已勾选。

## 1. 本版目标

让 TCP 连接在正常关闭、RST、半关闭下不泄漏、不丢包、不空转；让进程能优雅退出；堵住 TCP 会话越权；把构建、测试、文档拉回可用状态。HTTP demo 暂缓，不再投入新功能。

> 主线：B01–B06、B09、B10（TCP 业务链路）；B07/B08 属 HTTP demo，暂缓。

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

- [x] **B05 HTTP 服务无法优雅退出**（完成：2026-10-24）
  - 现象：Ctrl-C / SIGTERM 后进程不退出，卡在 `http_thread.join()`。
  - 发现：走查 `examples/chat_server/main.cpp:55-57` 与 `RunHttpServer` 的 `http.listen`（`examples/chat_server/HttpServer.cpp:1259`），没有 `stop()`；实际 Ctrl-C 复现。
  - 根因：信号处理只 `Quit()` 了 base_loop；httplib 的 listen 仍阻塞，join 永远等不到。
  - 修复：`HttpServer` 保存 `httplib::Server*` 并提供 `StopHttpServer()`；`main` 在 `server.Start()` 返回后先 `stop()` 再 `join()`；`EventLoop` 增加 `_quit_requested`，记录启动阶段收到的 `Quit`，避免 `Loop` 又把 `_running` 置回 true。
  - 验证：`chat_server` 实测运行中 SIGTERM/SIGINT 约 200ms 退出，启动阶段 SIGTERM 约 400ms 退出；8080/8888 端口释放。

### P1 · 安全与存储

- [x] **B06 TCP 会话状态与登出越权**（完成：2026-10-24）
  - 现象：`OnLogout` 直接使用客户端传来的 `user_id`，A 可以把 B 登出；同一连接可以重复登录；同一账号可以在多个连接同时在线，在线状态会互相覆盖。
  - 发现：审查 `OnLogin`/`OnLogout` 与各 handler 的 uid 来源，只有 `OnLogout` 用了 `req->user_id`（`src/service/ChatServer.cpp:379`）；`OnLogin` 没有检查当前 fd 是否已绑定；`_online_users[user_id]` 直接覆盖旧连接。
  - 根因：会话只靠 `_fd_to_user_id` 隐式维护，没有“登录/登出”的显式状态机；登出信任客户端字段，登录不拒绝重复绑定。
  - 修复：`OnLogout` 用 `GetUserIdByFd(conn->Fd())`，未登录返回错误；`OnLogin` 若当前 fd 已绑定或正在登录（`_login_pending`）则拒绝；`SetUserOnline` 检查同账号是否已有其他连接（单会话），有则返回失败；close 回调只在 `_online_users[uid] == conn` 时清理，且只有当前会话才把 Redis 置离线。
  - 验证：两个测试账号 A/B 的协议客户端：A 用 B 的 id 登出后 B 仍可请求好友列表；同连接重复登录被拒；第二连接登录同账号被拒且原连接仍可用；A 登出后业务请求返回“请先登录”。

> B07/B08 是 HTTP demo 的缺陷，当前 TCP 主线暂缓；HTTP 只保留本地 demo，不再投入鉴权/性能改造。等 TCP 主线完成后再决定是否回头修。

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

- [x] **B09 生命周期与启动边界**（完成：2026-10-24）
  - 现象：`EventLoop` 泄漏；`timeout=0` 启动崩溃；端口被占用时服务端“假启动”，第二个实例甚至能通过 `SO_REUSEPORT` 共享 8888。
  - 发现：`ChatServer.cpp:16` 裸 `new EventLoop` 无 delete；`TimeWheel` 构造未校验 capacity；`Acceptor` 构造忽略 `Bind`/`Listen` 返回值，且同时开启了 `SO_REUSEPORT`。
  - 根因：所有权和启动边界没有统一约束；单实例服务不应允许两个进程共享监听端口，启动失败也没有向上传递。
  - 修复：`_base_loop` 改为 `std::unique_ptr<EventLoop>`；`TimeWheel` 校验 `timeout > 0` 并抛出 `std::invalid_argument`；`Acceptor` 的 Bind/Listen 失败抛 `std::runtime_error`；`Acceptor` 只保留 `SO_REUSEADDR`，移除 `SO_REUSEPORT`；`main` 捕获启动异常并返回非 0。
  - 验证：`test_server_boundaries` 3/3；进程级第二个 `chat_server` 退出码 1 并打印 `bind failed on port 8888`；`timeout=0` 退出码 1 并打印 `TimeWheel timeout must be greater than 0`；ASan 留到 B10/CI 统一跑。

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
| v1.0 | 2026-10-23 | BUG 修复（本文件） | 进行中 7/8 主线 |
| v1.1 | 待定 | 业务统一 + WebSocket + 群聊 | 未开始 |

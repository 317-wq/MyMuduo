# MyMuduo 开发文档 · v1.1 IM 业务线

| 项 | 值 |
|---|---|
| 版本 | v1.1-im-flow |
| 基线 | v1.0 主线 8/8（`e5459e3`） |
| 目标 | 服务端-only；用一条可演示的 IM 业务线串起网络、协议、存储和可靠性 |
| 进度 | **1 / 8** |

## 0. 面试主线

白板上要能画出这一条：

```
注册/登录
  → 好友列表 / 添加好友
  → 私聊（落库 + msg_id + ack）
  → 历史消息 / 未读 / 已读
  → 建群 / 加群 / 群消息（在线 fanout）
  → 离线增量同步
  → 登出
```

讲的时候不要按“功能列表”讲，按**一条消息的生命周期**讲：

1. 客户端连上 TCP，登录后连接绑定 uid（单会话）。
2. 发私聊：服务端校验登录/好友 → 先写 MySQL → 拿到 `msg_id` → ack 发送者 → 在线则 push 接收者。
3. 接收者离线：消息已在库，登录后按 `msg_id` 增量拉取。
4. 群聊：先落库 `group_messages`，再查成员列表，对在线成员 fanout；离线成员靠同步。
5. 全链路用 `msg_id` 去重/排序；ack 负责发送方确认，sync 负责补洞。

## 1. 设计原则

- **服务端 only**：不做 Qt/产品客户端；CLI 只是协议测试工具。
- **先落库再推送**：消息的权威顺序在 MySQL，推送只是通知。
- **在线 push + 离线 pull**：不在线不丢消息，登录后按 last_id 增量同步。
- **msg_id 由服务端生成**：DB 自增 ID 作为全局顺序；客户端 `client_msg_id` 只用于 ack 匹配。
- **每个功能四件套**：协议、DB、测试、面试讲法。

## 2. 任务清单

- [x] **F01 私聊落库 + msg_id/ack**（完成：2026-10-24）
  - 目标：把 `OnPrivateMessage` 从“纯转发”改成“先落库、再 ack/push”。
  - 协议：`PrivateMessage` 增加 `msg_id`、`client_msg_id`；新增 `PrivateMessageAck`（`kPrivateMessageAck = 12`）。
  - DB：复用 `private_messages`；补 `init.sql` 缺失的 `is_revoked/reply_to_id/reply_preview/updated_at`。
  - 流程：校验登录 → 校验好友 → DB 插入 → ack 发送者 → 接收者在线则 push，离线只落库。
  - 测试：两个好友用户 A/B；A 发消息收到 ack（`msg_id > 0`）；B 收到 push；非好友发送被拒；DB 有记录。
  - 验证（2026-10-24）：临时协议客户端实测 ack `msg_id=33`、push 一致；非好友 ack 失败；B 登出后 A 发送仍落库（`msg_id=34`，提示离线）；DB 中 A→B 两条、C→A 零条。
  - 面试：为什么先落库再推送；ack 与重发；离线消息为什么不能只靠 push。

- [ ] **F02 私聊历史 / 未读 / 已读**
  - 协议：`PrivateHistoryRequest/Response`、`UnreadRequest/Response`、`MarkReadRequest/Response`。
  - DB：复用 `GetConversation/MarkAsRead/GetUnreadCounts`。
  - 测试：分页/after_id、未读数、已读后归零。
  - 面试：增量拉取 vs 全量分页；未读计数放 MySQL 还是 Redis。

- [ ] **F03 离线增量同步**
  - 协议：`SyncRequest(last_private_id)` → `SyncResponse(messages)`。
  - 流程：登录成功后客户端主动 sync；按 `msg_id` 去重；不采用“登录瞬间推送全部离线消息”。
  - 测试：B 离线 → A 发消息 → B 登录 sync，拿到缺失消息且不重复。
  - 面试：at-least-once + 客户端去重；sync 的幂等性。

- [ ] **F04 群聊基础**
  - DB：`chat_groups`、`group_members`。
  - 协议：建群 / 加群 / 退群 / 群列表。
  - 代码：`GroupDao` + `GroupService`，不要把群逻辑塞进 `ChatServer`。
  - 测试：建群者自动入群、重复加群、退群、群列表。
  - 面试：群成员关系怎么存；为什么成员表要联合主键。

- [ ] **F05 群消息 / fanout / 历史**
  - DB：`group_messages`。
  - 协议：`GroupMessage` 发送/推送、`GroupHistoryRequest/Response`。
  - 流程：先落库 → 查成员 → 在线成员逐个 push；离线成员靠 sync。
  - 测试：3 人群，2 人在线 1 人离线；在线两人收到，离线者 sync 补到。
  - 面试：fanout 写扩散 vs 读扩散；大群怎么优化。

- [ ] **F06 群离线 / 已读（可选）**
  - DB：`group_members.last_read_msg_id` 或独立已读表。
  - 目标：群未读数、@ 提醒；时间不够可后置。

- [ ] **F07 CLI 测试客户端 + 集成测试**
  - 工具：`tools/im_cli`（阻塞 socket + Codec），命令：login / friends / chat / history / group / sync。
  - 测试：把 F01–F05 串成一条脚本；面试现场可跑。
  - 注意：这是测试工具，不是产品客户端。

- [ ] **F08 压测 / ASan / CI + 面试讲稿**
  - `tcp_bench` 扩展成 IM 消息流；记录连接数、QPS、P99。
  - ASan/TSan、GitHub Actions（proto + lifecycle + boundaries）。
  - 输出 `docs/interview_story.md`：1 分钟 / 5 分钟 / 15 分钟版本 + 20 个追问。

## 3. 顺序与依赖

`F01 → F02 → F03 → F04 → F05 → F07 → F08`；F06 可选。

F01/F02/F03 是一条连续的消息可靠性线；F04/F05 是群聊线；F07 把两条线串成演示脚本；F08 补证据。

## 4. 不在本版范围

- Qt / 产品级客户端。
- HTTP demo 的新功能（B07/B08 继续暂缓）。
- 分布式多进程、跨机路由、消息队列。
- 富媒体、撤回、回复引用（TCP 主线暂不做）。

## 5. 完成定义

- 每个 F 项：协议/DB/测试/面试讲法四件套齐全。
- `ctest` 全绿；新增集成脚本可重复运行。
- `docs/interview_story.md` 能按一条业务线讲 15 分钟。
- 不引入未实现的功能描述；README 与代码一致。

## 6. 版本记录

| 版本 | 日期 | 主题 | 状态 |
|---|---|---|---|
| v1.1 | 2026-10-24 | IM 业务线（F01–F08） | 进行中 1/8 |

#include "service/ChatServer.h"
#include "net/Log.h"
#include "base/Timestamp.h"
#include "db/FriendDao.h"
#include "db/PrivateMessageDao.h"
#include "db/UserDao.h"

#include "SimpleIni.h"

#include <fstream>

// 全局指针，用于信号处理
static ChatServer* g_server = nullptr;

// 构造 / 析构

ChatServer::ChatServer()
{
    _base_loop = std::make_unique<EventLoop>();
}

ChatServer::~ChatServer()
{
    Stop();
}

// 配置加载

bool ChatServer::TryLoadConfig()
{
    // 依次尝试常见路径
    static const char* kPaths[] = {
        "config.ini",       // 当前目录
        "../config.ini"    // 从 build/ 运行
    };

    for (const char* path : kPaths) {
        std::ifstream f(path);
        if (f.good()) {
            LoadConfig(path);
            return true;
        }
    }
    return false;
}

void ChatServer::LoadConfig(const std::string& path)
{
    CSimpleIniA ini;
    SI_Error rc = ini.LoadFile(path.c_str());
    if (rc < 0) {
        LOG_WARNING("Failed to read config file: %s, using defaults", path.c_str());
        return;
    }

    _db_host     = ini.GetValue("database", "host", "127.0.0.1");
    _db_port     = static_cast<int>(ini.GetLongValue("database", "port", 3306));
    _db_user     = ini.GetValue("database", "user", "root");
    _db_pass     = ini.GetValue("database", "password", "");
    _db_name     = ini.GetValue("database", "database", "mymuduo");
    _db_pool_size = static_cast<int>(ini.GetLongValue("database", "pool_size", 4));

    _redis_host      = ini.GetValue("redis", "host", "127.0.0.1");
    _redis_port      = static_cast<int>(ini.GetLongValue("redis", "port", 6379));
    _redis_pool_size = static_cast<int>(ini.GetLongValue("redis", "pool_size", 4));

    _server_port = static_cast<int>(ini.GetLongValue("server", "port", 8888));
    _thread_num  = static_cast<int>(ini.GetLongValue("server", "threads", 4));
    _timeout     = static_cast<int>(ini.GetLongValue("server", "timeout", 60));

    _email_cfg.smtp_host = ini.GetValue("email", "smtp_host", "");
    _email_cfg.smtp_port = static_cast<int>(ini.GetLongValue("email", "smtp_port", 587));
    _email_cfg.smtp_user = ini.GetValue("email", "smtp_user", "");
    _email_cfg.smtp_pass = ini.GetValue("email", "smtp_password", "");
    _email_cfg.from_name = ini.GetValue("email", "from_name", "MyMuduo");

    LOG_INFO("Config loaded from %s: server=%d, db=%s:%d/%s, pool=%d, threads=%d, "
             "redis=%s:%d, smtp=%s:%d",
             path.c_str(), _server_port, _db_host.c_str(), _db_port,
             _db_name.c_str(), _db_pool_size, _thread_num,
             _redis_host.c_str(), _redis_port,
             _email_cfg.smtp_host.c_str(), _email_cfg.smtp_port);
}

// ============================================================
// 启动
// ============================================================

void ChatServer::Start()
{
    if (!TryLoadConfig()) {
        LOG_WARNING("No config.ini found, using default settings");
    }

    g_server = this;

    // 这个 Database 和 RedisCache 都是生产消费模型，多线程+连接池+任务队列的放肆
    // 1. 初始化数据库（如果尚未由外部设置）
    if (!_db) {
        _db = std::make_unique<Database>(
            _db_host, _db_port, _db_user, _db_pass, _db_name, _db_pool_size);
    }
    LOG_INFO("Database connected: %s:%d/%s", _db_host.c_str(), _db_port, _db_name.c_str());

    // 2. 初始化 Redis 缓存（可选，连接失败则降级为纯 MySQL 模式）
    try {
        _redis = std::make_unique<RedisCache>(_redis_host, _redis_port, _redis_pool_size);
        LOG_INFO("Redis cache connected: %s:%d (pool=%d)",
                 _redis_host.c_str(), _redis_port, _redis_pool_size);
    } catch (...) {
        LOG_WARNING("Redis not available, running without cache");
        _redis.reset();
    }

    // 3. 初始化用户服务
    _user_service = std::make_unique<UserService>(_db.get(), _base_loop.get());
    _user_service->SetEmailConfig(_email_cfg);
    if (_redis) {
        _user_service->SetRedisCache(_redis.get());
    }

    // 4. 初始化好友服务
    _friend_service = std::make_unique<FriendService>(_db.get(), _base_loop.get());
    if (_redis) {
        _friend_service->SetRedisCache(_redis.get());
    }

    // 初始化群聊服务
    _group_service = std::make_unique<GroupService>(_db.get(), _base_loop.get());

    // 3. 初始化 TcpServer
    _server = std::make_unique<TcpServer>(
        _base_loop.get(), static_cast<uint16_t>(_server_port),
        _thread_num, _timeout);

    // 4. 设置连接回调[就是tcpserver通知TcpConnection来处理这个连接，对其进行管理和设置]
    _server->SetConnectCallback(
        [this](const TcpConnection::Ptr& conn) {
            OnConnection(conn);
        });

    // 5. 设置连接关闭通知（底层 close 回调仍由 TcpServer 维护）
    _server->SetCloseNotifyCallback(
        [this](const TcpConnection::Ptr& conn) {
            OnConnectionClosed(conn);
        });

    // 6. 设置消息回调（TcpServer → Codec）
    _server->SetMessageCallback(
        [this](const TcpConnection::Ptr& conn, Buffer* buf) {
            OnRawMessage(conn, buf);
        });

    // 7. Codec → Dispatcher
    _codec.SetMessageCallback(
        [this](const TcpConnection::Ptr& conn, Message::Ptr msg) {
            _dispatcher.Dispatch(conn, std::move(msg), Timestamp::Now());
        });

    // 8. 注册 Dispatcher 处理器
    _dispatcher.Register(MessageType::kSendCodeRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnSendCode(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kRegisterRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnRegister(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kLoginRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnLogin(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kLogoutRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnLogout(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kHeartbeat,
        [this](auto& conn, auto msg, auto ts) {
            OnHeartbeat(conn, std::move(msg), ts);
        });

    // 好友相关
    _dispatcher.Register(MessageType::kSearchUserRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnSearchUser(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kAddFriendRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnAddFriend(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kAcceptFriendRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnAcceptFriend(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kDeleteFriendRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnDeleteFriend(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kFriendListRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnFriendList(conn, std::move(msg), ts);
        });

    // 群聊基础
    _dispatcher.Register(MessageType::kCreateGroupRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnCreateGroup(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kJoinGroupRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnJoinGroup(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kLeaveGroupRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnLeaveGroup(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kGroupListRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnGroupList(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kGroupMessage,
        [this](auto& conn, auto msg, auto ts) {
            OnGroupMessage(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kGroupHistoryRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnGroupHistory(conn, std::move(msg), ts);
        });

    // 私聊消息
    _dispatcher.Register(MessageType::kPrivateMessage,
        [this](auto& conn, auto msg, auto ts) {
            OnPrivateMessage(conn, std::move(msg), ts);
        });

    // 私聊历史 / 未读 / 已读
    _dispatcher.Register(MessageType::kPrivateHistoryRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnPrivateHistory(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kUnreadRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnUnread(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kMarkReadRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnMarkRead(conn, std::move(msg), ts);
        });
    _dispatcher.Register(MessageType::kSyncRequest,
        [this](auto& conn, auto msg, auto ts) {
            OnSync(conn, std::move(msg), ts);
        });

    // 9. 启动 TcpServer
    _server->Start();
    LOG_INFO("ChatServer started on port %d", _server_port);

    // 9. 进入事件循环（阻塞）
    _base_loop->Loop();
}

void ChatServer::Stop()
{
    if (_server) {
        _server.reset();
    }
    if (_db) {
        _db.reset();
    }
    if (_base_loop) {
        _base_loop->Quit();
    }
    g_server = nullptr;
}

// ============================================================
// TcpServer 回调
// ============================================================

void ChatServer::OnConnection(const TcpConnection::Ptr& conn)
{
    LOG_INFO("New connection: fd=%d", conn->Fd());
}

// 连接关闭后由 TcpServer 在 base_loop 中回调，清理 fd→user_id 映射和在线状态。
void ChatServer::OnConnectionClosed(const TcpConnection::Ptr& conn)
{
    int fd = conn->Fd();
    uint32_t user_id = 0;
    {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        auto it = _fd_to_user_id.find(fd);
        if (it != _fd_to_user_id.end()) {
            user_id = it->second;
            _fd_to_user_id.erase(it);
        }
        _login_pending.erase(fd);
    }
    if (user_id > 0) {
        bool was_current_session = false;
        {
            std::lock_guard<std::mutex> lock(_online_mutex);
            auto it = _online_users.find(user_id);
            if (it != _online_users.end() && it->second == conn) {
                _online_users.erase(it);
                was_current_session = true;
            }
        }
        // 只有关闭的是当前会话时才把 Redis 标记为离线，避免旧连接误伤新会话。
        if (was_current_session && _redis) {
            _redis->Execute(nullptr,
                [user_id](sw::redis::Redis* ctx) {
                    RedisDao::SetUserOffline(ctx, user_id);
                },
                nullptr);
        }
    }
    LOG_INFO("Connection closed: fd=%d user_id=%u", fd, user_id);
}

void ChatServer::OnRawMessage(const TcpConnection::Ptr& conn, Buffer* buf)
{
    _codec.OnMessage(conn, buf);
}

// ============================================================
// Dispatcher 处理器
// ============================================================

// ---------- 发送验证码 ----------
void ChatServer::OnSendCode(const TcpConnection::Ptr& conn,
                            Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<SendCodeRequest*>(msg.get());
    if (!req || req->email.empty()) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少邮箱"});
        return;
    }

    LOG_INFO("SendCode request: email=%s type=%d", req->email.c_str(), req->type);

    // 捕获弱引用，回调时检查连接是否还在
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _user_service->SendVerificationCode(req->email, req->type,
        [this, weak_conn](int err, const std::string& msg_str) {
            auto conn = weak_conn.lock();
            if (!conn) return;

            SendCodeResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            SendMessage(conn, resp);
        });
}

// ---------- 验证码注册 ----------
void ChatServer::OnRegister(const TcpConnection::Ptr& conn,
                            Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<RegisterRequest*>(msg.get());
    if (!req || req->email.empty() || req->password.empty()) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少邮箱或密码"});
        return;
    }

    LOG_INFO("Register request: email=%s username=%s",
             req->email.c_str(), req->username.c_str());

    std::weak_ptr<TcpConnection> weak_conn = conn;

    _user_service->RegisterWithCode(
        req->email, req->code, req->password, req->username,
        [this, weak_conn](int err, const std::string& msg_str, uint32_t user_id) {
            auto conn = weak_conn.lock();
            if (!conn) return;

            RegisterResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            resp.user_id = user_id;
            SendMessage(conn, resp);
        });
}

// ---------- 登录 ----------
void ChatServer::OnLogin(const TcpConnection::Ptr& conn,
                         Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<LoginRequest*>(msg.get());
    if (!req || req->email.empty() || req->password.empty()) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少邮箱或密码"});
        return;
    }

    // 一个连接只允许绑定一个账号；已登录或正在登录都拒绝，换号必须先登出。
    int login_fd = conn->Fd();
    bool login_busy = false;
    {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        login_busy = (_fd_to_user_id.count(login_fd) != 0)
                  || (_login_pending.count(login_fd) != 0);
        if (!login_busy) {
            _login_pending.insert(login_fd);
        }
    }
    if (login_busy) {
        SendMessage(conn, ErrorMessage{3, "当前连接已登录或正在登录"});
        return;
    }

    LOG_INFO("Login request: email=%s", req->email.c_str());

    std::weak_ptr<TcpConnection> weak_conn = conn;

    _user_service->Login(req->email, req->password,
        [this, weak_conn, login_fd](int err, const std::string& msg_str, const UserInfo& user) {
            // 登录回调返回后清除 pending，再处理结果。
            {
                std::lock_guard<std::mutex> lock(_fd_mutex);
                _login_pending.erase(login_fd);
            }
            auto conn = weak_conn.lock();
            if (!conn) return;

            if (err == 0) {
                // 登录成功，记录在线状态；同一账号只允许一个活动连接。
                if (!SetUserOnline(user.id, conn)) {
                    SendMessage(conn, ErrorMessage{3, "该账号已在其他设备登录"});
                    return;
                }
                LOG_INFO("User logged in: id=%u email=%s username=%s",
                         user.id, user.email.c_str(), user.username.c_str());
            }

            LoginResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            resp.user_id = user.id;
            resp.username = user.username;
            resp.avatar = user.avatar;
            SendMessage(conn, resp);
        });
}

// ---------- 登出 ----------
void ChatServer::OnLogout(const TcpConnection::Ptr& conn,
                          Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<LogoutRequest*>(msg.get());
    if (!req) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    // 登出身份只认连接绑定的 uid，忽略客户端声明的 user_id，防止越权登出。
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (req->user_id != 0 && req->user_id != my_id) {
        LOG_WARNING("Logout user_id mismatch: fd=%d session=%u request=%u",
                    conn->Fd(), my_id, req->user_id);
    }

    LOG_INFO("Logout: user_id=%u fd=%d", my_id, conn->Fd());
    SetUserOffline(my_id);

    LogoutResponse resp;
    resp.success = true;
    resp.message = "已登出";
    SendMessage(conn, resp);
}

// ---------- 心跳 ----------
void ChatServer::OnHeartbeat(const TcpConnection::Ptr& conn,
                             Message::Ptr msg, Timestamp ts)
{
    (void)msg;
    (void)ts;
    // 心跳直接回复
    HeartbeatMessage resp;
    SendMessage(conn, resp);
}

// ============================================================
// 在线用户管理
// ============================================================

bool ChatServer::SetUserOnline(uint32_t user_id, const TcpConnection::Ptr& conn)
{
    {
        std::lock_guard<std::mutex> lock(_online_mutex);
        auto it = _online_users.find(user_id);
        if (it != _online_users.end() && it->second != conn) {
            // 单会话策略：同一账号已有活动连接，拒绝新连接登录。
            return false;
        }
        _online_users[user_id] = conn;
    }
    {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        _fd_to_user_id[conn->Fd()] = user_id;
    }
    // Redis 在线状态
    if (_redis) {
        _redis->Execute(nullptr,
            [user_id](sw::redis::Redis* ctx) {
                RedisDao::SetUserOnline(ctx, user_id, 180);  // 3 分钟
            },
            nullptr);
    }
    return true;
}

void ChatServer::SetUserOffline(uint32_t user_id)
{
    // 先获取 fd 用于清理映射
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(_online_mutex);
        auto it = _online_users.find(user_id);
        if (it != _online_users.end()) {
            fd = it->second->Fd();
        }
        _online_users.erase(user_id);
    }
    if (fd >= 0) {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        _fd_to_user_id.erase(fd);
    }
    // Redis 离线
    if (_redis) {
        _redis->Execute(nullptr,
            [user_id](sw::redis::Redis* ctx) {
                RedisDao::SetUserOffline(ctx, user_id);
            },
            nullptr);
    }
}

TcpConnection::Ptr ChatServer::GetConnectionByUserId(uint32_t user_id)
{
    std::lock_guard<std::mutex> lock(_online_mutex);
    auto it = _online_users.find(user_id);
    if (it != _online_users.end()) {
        return it->second;
    }
    return nullptr;
}

uint32_t ChatServer::GetUserIdByFd(int fd)
{
    std::lock_guard<std::mutex> lock(_fd_mutex);
    auto it = _fd_to_user_id.find(fd);
    if (it != _fd_to_user_id.end()) {
        return it->second;
    }
    return 0;
}

// ============================================================
// 好友相关处理器
// ============================================================

void ChatServer::OnSearchUser(const TcpConnection::Ptr& conn,
                              Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<SearchUserRequest*>(msg.get());
    if (!req || req->keyword.empty()) {
        SendMessage(conn, ErrorMessage{1, "请输入搜索关键词"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    LOG_INFO("SearchUser: fd=%d user_id=%u keyword=%s",
             conn->Fd(), my_id, req->keyword.c_str());

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _friend_service->SearchUser(req->keyword, my_id,
        [this, weak_conn](int err, const std::string& msg,
                          const std::vector<UserInfo>& users) {
            auto c = weak_conn.lock();
            if (!c) return;

            SearchUserResponse resp;
            resp.success = (err == 0);
            for (auto& u : users) {
                SearchUserResponse::UserItem item;
                item.id = u.id;
                item.email = u.email;
                item.username = u.username;
                item.avatar = u.avatar;
                resp.users.push_back(item);
            }
            SendMessage(c, resp);
        });
}

void ChatServer::OnAddFriend(const TcpConnection::Ptr& conn,
                             Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<AddFriendRequest*>(msg.get());
    if (!req || req->to_user_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    LOG_INFO("AddFriend: from=%u to=%u", my_id, req->to_user_id);

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _friend_service->SendFriendRequest(my_id, req->to_user_id,
        [this, weak_conn](int err, const std::string& msg_str) {
            auto c = weak_conn.lock();
            if (!c) return;
            AddFriendResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            SendMessage(c, resp);
        });
}

void ChatServer::OnAcceptFriend(const TcpConnection::Ptr& conn,
                                Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<AcceptFriendRequest*>(msg.get());
    if (!req || req->request_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    LOG_INFO("AcceptFriend: user=%u request_id=%u", my_id, req->request_id);

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _friend_service->AcceptFriendRequest(my_id, req->request_id,
        [this, weak_conn](int err, const std::string& msg_str,
                          uint32_t friend_id, const std::string& friend_email,
                          const std::string& friend_username,
                          const std::string& friend_avatar) {
            auto c = weak_conn.lock();
            if (!c) return;
            AcceptFriendResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            resp.friend_id = friend_id;
            resp.friend_email = friend_email;
            resp.friend_username = friend_username;
            resp.friend_avatar = friend_avatar;
            SendMessage(c, resp);
        });
}

void ChatServer::OnDeleteFriend(const TcpConnection::Ptr& conn,
                                Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<DeleteFriendRequest*>(msg.get());
    if (!req || req->friend_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    LOG_INFO("DeleteFriend: user=%u friend=%u", my_id, req->friend_id);

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _friend_service->DeleteFriend(my_id, req->friend_id,
        [this, weak_conn](int err, const std::string& msg_str) {
            auto c = weak_conn.lock();
            if (!c) return;
            DeleteFriendResponse resp;
            resp.success = (err == 0);
            resp.message = msg_str;
            SendMessage(c, resp);
        });
}

void ChatServer::OnFriendList(const TcpConnection::Ptr& conn,
                              Message::Ptr msg, Timestamp ts)
{
    (void)msg;
    (void)ts;

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _friend_service->GetFriendList(my_id,
        [this, weak_conn](int err, const std::string& msg_str,
                          const std::vector<FriendInfo>& friends) {
            auto c = weak_conn.lock();
            if (!c) return;
            FriendListResponse resp;
            resp.success = (err == 0);
            for (auto& f : friends) {
                FriendListResponse::FriendItem item;
                item.id = f.friend_id;
                item.email = f.email;
                item.username = f.username;
                item.avatar = f.avatar;
                item.remark = f.remark;
                item.online = f.online;
                resp.friends.push_back(item);
            }
            SendMessage(c, resp);
        });
}

// ---------- 群聊基础 ----------
void ChatServer::OnCreateGroup(const TcpConnection::Ptr& conn,
                               Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<CreateGroupRequest*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _group_service->CreateGroup(my_id, req->name,
        [this, weak_conn](int err, const std::string& message, uint32_t group_id) {
            auto c = weak_conn.lock();
            if (!c) return;
            CreateGroupResponse resp;
            resp.success = (err == 0);
            resp.message = message;
            resp.group_id = group_id;
            SendMessage(c, resp);
        });
}

void ChatServer::OnJoinGroup(const TcpConnection::Ptr& conn,
                             Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<JoinGroupRequest*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req || req->group_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少群 ID"});
        return;
    }

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _group_service->JoinGroup(my_id, req->group_id,
        [this, weak_conn](int err, const std::string& message, const GroupInfo& info) {
            auto c = weak_conn.lock();
            if (!c) return;
            JoinGroupResponse resp;
            resp.success = (err == 0);
            resp.message = message;
            resp.group_id = info.id;
            resp.group_name = info.name;
            resp.member_count = info.member_count;
            resp.my_role = info.my_role;
            SendMessage(c, resp);
        });
}

void ChatServer::OnLeaveGroup(const TcpConnection::Ptr& conn,
                              Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<LeaveGroupRequest*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req || req->group_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少群 ID"});
        return;
    }

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _group_service->LeaveGroup(my_id, req->group_id,
        [this, weak_conn](int err, const std::string& message) {
            auto c = weak_conn.lock();
            if (!c) return;
            LeaveGroupResponse resp;
            resp.success = (err == 0);
            resp.message = message;
            SendMessage(c, resp);
        });
}

void ChatServer::OnGroupList(const TcpConnection::Ptr& conn,
                             Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<GroupListRequest*>(msg.get());
    if (!req) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    std::weak_ptr<TcpConnection> weak_conn = conn;
    _group_service->ListGroups(my_id,
        [this, weak_conn](int err, const std::string& message,
                          const std::vector<GroupInfo>& groups) {
            auto c = weak_conn.lock();
            if (!c) return;
            GroupListResponse resp;
            resp.success = (err == 0);
            resp.message = message;
            for (const auto& g : groups) {
                GroupListResponse::Item item;
                item.group_id = g.id;
                item.name = g.name;
                item.owner_id = g.owner_id;
                item.member_count = g.member_count;
                item.my_role = g.my_role;
                item.created_at = g.created_at;
                resp.groups.push_back(std::move(item));
            }
            SendMessage(c, resp);
        });
}

// ---------- 群消息 / 群历史 ----------
void ChatServer::OnGroupMessage(const TcpConnection::Ptr& conn,
                                Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<GroupMessage*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req || req->group_id == 0 || req->content.empty()) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少群或内容"});
        return;
    }

    const uint32_t group_id = req->group_id;
    const uint32_t client_msg_id = req->client_msg_id;
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _group_service->SendGroupMessage(my_id, group_id, req->content,
        [this, weak_conn, my_id, group_id, client_msg_id]
        (int err, const std::string& message,
         const GroupMessageRecord& record,
         const std::vector<uint32_t>& members) {
            auto c = weak_conn.lock();
            if (!c) return;

            GroupMessageAck ack;
            ack.group_id = group_id;
            ack.client_msg_id = client_msg_id;
            ack.msg_id = record.id;
            ack.success = (err == 0);
            ack.message = message;
            SendMessage(c, ack);

            if (err != 0) return;

            GroupMessage push;
            push.group_id = record.group_id;
            push.msg_id = record.id;
            push.client_msg_id = client_msg_id;
            push.from_user_id = record.from_user_id;
            push.from_username = record.from_username;
            push.content = record.content;
            push.timestamp = Timestamp::Now().MilliSecondsSinceEpoch();
            push.created_at = record.created_at;

            int delivered = 0;
            for (uint32_t member_id : members) {
                if (member_id == record.from_user_id) continue;
                TcpConnection::Ptr target = GetConnectionByUserId(member_id);
                if (target) {
                    SendMessage(target, push);
                    ++delivered;
                }
            }
            LOG_INFO("GroupMessage delivered: msg_id=%u group=%u from=%u online=%d/%zu",
                     record.id, group_id, my_id, delivered, members.size());
        });
}

void ChatServer::OnGroupHistory(const TcpConnection::Ptr& conn,
                                Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<GroupHistoryRequest*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req || req->group_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少群 ID"});
        return;
    }

    const uint32_t group_id = req->group_id;
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _group_service->GetGroupHistory(my_id, group_id, req->after_id, req->limit,
        [this, weak_conn, group_id]
        (int err, const std::string& message,
         const std::vector<GroupMessageRecord>& records,
         bool has_more, uint32_t last_msg_id) {
            auto c = weak_conn.lock();
            if (!c) return;

            GroupHistoryResponse resp;
            resp.success = (err == 0);
            resp.message = message;
            resp.group_id = group_id;
            resp.has_more = has_more;
            resp.last_msg_id = last_msg_id;
            for (const auto& r : records) {
                GroupMessage m;
                m.group_id = r.group_id;
                m.msg_id = r.id;
                m.from_user_id = r.from_user_id;
                m.from_username = r.from_username;
                m.content = r.content;
                m.created_at = r.created_at;
                resp.messages.push_back(std::move(m));
            }
            SendMessage(c, resp);
        });
}

// ---------- 私聊消息 ----------
void ChatServer::OnPrivateMessage(const TcpConnection::Ptr& conn,
                                   Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<PrivateMessage*>(msg.get());
    if (!req || req->content.empty()) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    // 发送者身份只认连接绑定的 uid，不接受客户端声明的 from_user_id。
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    uint32_t to_user_id = req->to_user_id;
    if (to_user_id == 0 || to_user_id == my_id) {
        SendMessage(conn, ErrorMessage{1, "参数错误：无效的接收者"});
        return;
    }
    if (req->content.size() > 65535) {
        SendMessage(conn, ErrorMessage{1, "消息内容过长"});
        return;
    }

    const uint32_t client_msg_id = req->client_msg_id;
    const std::string content = req->content;

    struct Result {
        bool ok = false;
        uint32_t msg_id = 0;
        std::string message;
        std::string from_username;
    };
    auto result = std::make_shared<Result>();
    std::weak_ptr<TcpConnection> weak_conn = conn;

    // 先落库，再 ack/push：消息的权威顺序在 MySQL，推送只是通知。
    _db->Execute(_base_loop.get(),
        [my_id, to_user_id, content, result](sql::Connection* db_conn) {
            UserInfo sender;
            if (!UserDao::GetUserById(db_conn, my_id, sender)) {
                result->message = "发送者不存在";
                return;
            }
            result->from_username = sender.username;

            if (!FriendDao::IsFriend(db_conn, my_id, to_user_id)) {
                result->message = "你们还不是好友，无法发送私聊";
                return;
            }

            if (!PrivateMessageDao::SendMessage(
                    db_conn, my_id, to_user_id, content, result->msg_id)) {
                result->message = "消息保存失败";
                return;
            }

            result->ok = true;
            result->message = "发送成功";
        },
        [this, weak_conn, to_user_id, content, client_msg_id, result]() {
            auto conn = weak_conn.lock();
            if (!conn) return;

            TcpConnection::Ptr target =
                result->ok ? GetConnectionByUserId(to_user_id) : nullptr;

            PrivateMessageAck ack;
            ack.client_msg_id = client_msg_id;
            ack.msg_id = result->msg_id;
            ack.success = result->ok;
            ack.message = result->message;
            if (result->ok && !target) {
                ack.message = "发送成功（对方离线，已保存）";
            }
            SendMessage(conn, ack);

            if (!result->ok) {
                LOG_INFO("PrivateMessage rejected: from=%u to=%u reason=%s",
                         GetUserIdByFd(conn->Fd()), to_user_id,
                         result->message.c_str());
                return;
            }

            if (target) {
                PrivateMessage push;
                push.msg_id = result->msg_id;
                push.client_msg_id = client_msg_id;
                push.from_user_id = GetUserIdByFd(conn->Fd());
                push.from_username = result->from_username;
                push.to_user_id = to_user_id;
                push.content = content;
                push.timestamp = Timestamp::Now().MilliSecondsSinceEpoch();
                SendMessage(target, push);
                LOG_INFO("PrivateMessage delivered: msg_id=%u %u -> %u",
                         result->msg_id, push.from_user_id, to_user_id);
            } else {
                LOG_INFO("PrivateMessage stored offline: msg_id=%u to=%u",
                         result->msg_id, to_user_id);
            }
        });
}

// ---------- 私聊历史 ----------
void ChatServer::OnPrivateHistory(const TcpConnection::Ptr& conn,
                                  Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<PrivateHistoryRequest*>(msg.get());
    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }
    if (!req || req->friend_id == 0 || req->friend_id == my_id) {
        SendMessage(conn, ErrorMessage{1, "参数错误：无效的好友"});
        return;
    }

    const uint32_t friend_id = req->friend_id;
    const uint32_t after_id = req->after_id;
    int limit = req->limit;
    if (limit <= 0) limit = 50;
    if (limit > 200) limit = 200;

    auto result = std::make_shared<PrivateHistoryResponse>();
    result->friend_id = friend_id;
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _db->Execute(_base_loop.get(),
        [my_id, friend_id, after_id, limit, result](sql::Connection* db_conn) {
            std::vector<PrivateMessageRecord> messages;
            std::vector<PrivateMessageRecord> updates;
            if (!PrivateMessageDao::GetConversation(
                    db_conn, my_id, friend_id, after_id, limit,
                    messages, updates, "")) {
                result->message = "查询失败";
                return;
            }

            result->success = true;
            result->message = "ok";
            for (const auto& m : messages) {
                PrivateHistoryResponse::Item item;
                item.msg_id = m.id;
                item.from_user_id = m.from_user_id;
                item.to_user_id = m.to_user_id;
                item.content = m.content;
                item.is_read = m.is_read;
                item.is_revoked = m.is_revoked;
                item.reply_to_id = m.reply_to_id;
                item.reply_preview = m.reply_preview;
                item.created_at = m.created_at;
                item.updated_at = m.updated_at;
                result->messages.push_back(std::move(item));
            }
        },
        [this, weak_conn, result]() {
            auto conn = weak_conn.lock();
            if (!conn) return;
            SendMessage(conn, *result);
        });
}

// ---------- 未读数 ----------
void ChatServer::OnUnread(const TcpConnection::Ptr& conn,
                          Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<UnreadRequest*>(msg.get());
    if (!req) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    auto result = std::make_shared<UnreadResponse>();
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _db->Execute(_base_loop.get(),
        [my_id, result](sql::Connection* db_conn) {
            result->counts = PrivateMessageDao::GetUnreadCounts(db_conn, my_id);
            result->total = 0;
            for (const auto& kv : result->counts) {
                result->total += kv.second;
            }
            result->success = true;
        },
        [this, weak_conn, result]() {
            auto conn = weak_conn.lock();
            if (!conn) return;
            SendMessage(conn, *result);
        });
}

// ---------- 标记已读 ----------
void ChatServer::OnMarkRead(const TcpConnection::Ptr& conn,
                            Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<MarkReadRequest*>(msg.get());
    if (!req || req->friend_id == 0) {
        SendMessage(conn, ErrorMessage{1, "参数错误：缺少好友"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    const uint32_t friend_id = req->friend_id;
    auto result = std::make_shared<MarkReadResponse>();
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _db->Execute(_base_loop.get(),
        [my_id, friend_id, result](sql::Connection* db_conn) {
            result->success = PrivateMessageDao::MarkAsRead(db_conn, my_id, friend_id);
            result->message = result->success ? "已标记已读" : "标记失败";
        },
        [this, weak_conn, result]() {
            auto conn = weak_conn.lock();
            if (!conn) return;
            SendMessage(conn, *result);
        });
}

// ---------- 离线增量同步 ----------
void ChatServer::OnSync(const TcpConnection::Ptr& conn,
                        Message::Ptr msg, Timestamp ts)
{
    (void)ts;
    auto* req = dynamic_cast<SyncRequest*>(msg.get());
    if (!req) {
        SendMessage(conn, ErrorMessage{1, "参数错误"});
        return;
    }

    uint32_t my_id = GetUserIdByFd(conn->Fd());
    if (my_id == 0) {
        SendMessage(conn, ErrorMessage{2, "请先登录"});
        return;
    }

    const uint32_t last_private_id = req->last_private_id;
    int limit = req->limit;
    if (limit <= 0) limit = 200;
    if (limit > 200) limit = 200;

    auto result = std::make_shared<SyncResponse>();
    result->last_msg_id = last_private_id;
    result->message = "同步失败";
    std::weak_ptr<TcpConnection> weak_conn = conn;

    _db->Execute(_base_loop.get(),
        [my_id, last_private_id, limit, result](sql::Connection* db_conn) {
            std::vector<PrivateMessageRecord> records;
            bool has_more = false;
            if (!PrivateMessageDao::GetIncomingAfter(
                    db_conn, my_id, last_private_id, limit, records, has_more)) {
                return;
            }

            result->success = true;
            result->message = "ok";
            result->has_more = has_more;
            for (const auto& r : records) {
                PrivateMessage m;
                m.msg_id = r.id;
                m.from_user_id = r.from_user_id;
                m.from_username = r.from_username;
                m.to_user_id = r.to_user_id;
                m.content = r.content;
                m.created_at = r.created_at;
                if (r.id > result->last_msg_id) {
                    result->last_msg_id = r.id;
                }
                result->messages.push_back(std::move(m));
            }
        },
        [this, weak_conn, result]() {
            auto conn = weak_conn.lock();
            if (!conn) return;
            SendMessage(conn, *result);
        });
}

// ============================================================
// 工具方法
// ============================================================

void ChatServer::SendMessage(const TcpConnection::Ptr& conn, const Message& msg)
{
    std::string encoded = _codec.Encode(msg); // 序列化传输
    conn->Send(encoded);
}

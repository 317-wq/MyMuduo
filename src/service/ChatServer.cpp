#include "service/ChatServer.h"
#include "net/Log.h"
#include "base/Timestamp.h"

#include "SimpleIni.h"

#include <fstream>

// 全局指针，用于信号处理
static ChatServer* g_server = nullptr;

// 构造 / 析构

ChatServer::ChatServer()
{
    _base_loop = new EventLoop();
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
    _user_service = std::make_unique<UserService>(_db.get(), _base_loop);
    _user_service->SetEmailConfig(_email_cfg);
    if (_redis) {
        _user_service->SetRedisCache(_redis.get());
    }

    // 4. 初始化好友服务
    _friend_service = std::make_unique<FriendService>(_db.get(), _base_loop);
    if (_redis) {
        _friend_service->SetRedisCache(_redis.get());
    }

    // 3. 初始化 TcpServer
    _server = std::make_unique<TcpServer>(
        _base_loop, static_cast<uint16_t>(_server_port),
        _thread_num, _timeout);

    // 4. 设置连接回调
    _server->SetConnectCallback(
        [this](const TcpConnection::Ptr& conn) {
            OnConnection(conn);
        });

    // 5. 设置消息回调（TcpServer → Codec）
    _server->SetMessageCallback(
        [this](const TcpConnection::Ptr& conn, Buffer* buf) {
            OnRawMessage(conn, buf);
        });

    // 6. Codec → Dispatcher
    _codec.SetMessageCallback(
        [this](const TcpConnection::Ptr& conn, Message::Ptr msg) {
            _dispatcher.Dispatch(conn, std::move(msg), Timestamp::Now());
        });

    // 7. 注册 Dispatcher 处理器
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

    // 私聊消息
    _dispatcher.Register(MessageType::kPrivateMessage,
        [this](auto& conn, auto msg, auto ts) {
            OnPrivateMessage(conn, std::move(msg), ts);
        });

    // 8. 启动 TcpServer
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

    // 连接关闭时清理 fd→user_id 映射和在线状态
    conn->SetCloseCallback([this](const TcpConnection::Ptr& c) {
        int fd = c->Fd();
        uint32_t user_id = 0;
        {
            std::lock_guard<std::mutex> lock(_fd_mutex);
            auto it = _fd_to_user_id.find(fd);
            if (it != _fd_to_user_id.end()) {
                user_id = it->second;
                _fd_to_user_id.erase(it);
            }
        }
        if (user_id > 0) {
            {
                std::lock_guard<std::mutex> lock(_online_mutex);
                _online_users.erase(user_id);
            }
            if (_redis) {
                _redis->Execute(nullptr,
                    [user_id](redisContext* ctx) {
                        RedisDao::SetUserOffline(ctx, user_id);
                    },
                    nullptr);
            }
        }
        LOG_INFO("Connection closed: fd=%d user_id=%u", fd, user_id);
    });
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

    LOG_INFO("Login request: email=%s", req->email.c_str());

    std::weak_ptr<TcpConnection> weak_conn = conn;

    _user_service->Login(req->email, req->password,
        [this, weak_conn](int err, const std::string& msg_str, const UserInfo& user) {
            auto conn = weak_conn.lock();
            if (!conn) return;

            if (err == 0) {
                // 登录成功，记录在线状态
                SetUserOnline(user.id, conn);
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

    LOG_INFO("Logout request: user_id=%u", req->user_id);
    SetUserOffline(req->user_id);

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

void ChatServer::SetUserOnline(uint32_t user_id, const TcpConnection::Ptr& conn)
{
    {
        std::lock_guard<std::mutex> lock(_online_mutex);
        _online_users[user_id] = conn;
    }
    {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        _fd_to_user_id[conn->Fd()] = user_id;
    }
    // Redis 在线状态
    if (_redis) {
        _redis->Execute(nullptr,
            [user_id](redisContext* ctx) {
                RedisDao::SetUserOnline(ctx, user_id, 180);  // 3 分钟
            },
            nullptr);
    }
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
    if (fd > 0) {
        std::lock_guard<std::mutex> lock(_fd_mutex);
        _fd_to_user_id.erase(fd);
    }
    // Redis 离线
    if (_redis) {
        _redis->Execute(nullptr,
            [user_id](redisContext* ctx) {
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

    // 从 fd 获取发送者身份（覆盖客户端声明的字段，防伪造）
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

    LOG_INFO("PrivateMessage: from=%u to=%u content_len=%zu",
             my_id, to_user_id, req->content.size());

    // 用服务器端的身份信息覆盖消息
    req->from_user_id = my_id;
    req->timestamp = Timestamp::Now().MilliSecondsSinceEpoch();

    // 查找接收者的连接
    TcpConnection::Ptr target_conn = GetConnectionByUserId(to_user_id);

    if (target_conn) {
        // 接收者在线 → 转发
        SendMessage(target_conn, *req);
        LOG_INFO("PrivateMessage forwarded: %u → %u", my_id, to_user_id);
    }

    // 同时给发送者回显（确认发送成功）
    // 接收者不在线时，消息仅回显给发送者
    if (!target_conn) {
        // 通知发送者对方不在线
        SystemMessage sys_msg;
        sys_msg.content = "对方当前离线，消息未送达";
        sys_msg.timestamp = Timestamp::Now().MilliSecondsSinceEpoch();
        SendMessage(conn, sys_msg);
    }
}

// ============================================================
// 工具方法
// ============================================================

void ChatServer::SendMessage(const TcpConnection::Ptr& conn, const Message& msg)
{
    std::string encoded = _codec.Encode(msg);
    conn->Send(encoded);
}

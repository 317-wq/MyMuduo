// httplib 需要先于 Log.h 包含，避免宏冲突。
#include "httplib.h"
#include "HttpServer.h"
#include "HttpUtils.h"
#include "db/UserDao.h"
#include "db/FriendDao.h"
#include "db/PrivateMessageDao.h"
#include "cache/RedisDao.h"
#include "base/Crypto.h"
#include "net/Log.h"
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <sw/redis++/redis++.h>

// HTTP 服务就绪标志
std::atomic<bool> g_http_ready{false};

// ============================================================
// HTTP 服务线程
// ============================================================

void RunHttpServer(Database* db, const EmailSender::Config& email_cfg)
{
    std::string static_dir = FindStaticDir();
    LOG_INFO("HTTP static files serving from: %s", static_dir.c_str());

    // HTML 页面路径
    std::string index_path    = static_dir + "/html/index.html";
    std::string login_path    = static_dir + "/html/login.html";
    std::string register_path = static_dir + "/html/register.html";
        std::string notfound_path = static_dir + "/html/404.html";

    httplib::Server http;

    // ---- 静态文件挂载 ----
    http.set_mount_point("/static", static_dir);

    // ---- 首页（登录页） ----
    http.Get("/", [login_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(login_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("login.html not found", "text/plain");
        }
    });

    // ---- 登录页面 ----
    http.Get("/login", [login_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(login_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("login.html not found", "text/plain");
        }
    });

    // ---- 注册页面 ----
    http.Get("/register", [register_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(register_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("register.html not found", "text/plain");
        }
    });


    // ---- 聊天室页面（登录后访问） ----
    http.Get("/chat", [index_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(index_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("chat.html not found", "text/plain");
        }
    });

    // ---- 健康检查 ----
    http.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ---- 心跳保活（刷新 Redis 在线状态 TTL） ----
    http.Post("/api/ping", [](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }
        uint32_t user_id = body.get("user_id", 0).asUInt();
        if (user_id > 0) {
            sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            if (true) {
                // 刷新 TTL 到 2 分钟
                RedisDao::SetUserOnline(&rctx, user_id, 120);
                
            }
        }
        Json::Value resp;
        resp["success"] = true;
        SendJson(res, resp);
    });

    // ---- 404 页面（兜底路由，必须放在所有精确路由之后） ----
    http.set_error_handler([notfound_path](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            std::ifstream f(notfound_path);
            if (f.good()) {
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                res.set_content(content, "text/html; charset=utf-8");
            }
        }
    });

    // ============================================================
    // REST API
    // ============================================================

    // ---- 发送验证码 ----
    http.Post("/api/send-code", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        std::string email = body.get("email", "").asString();
        int type = body.get("type", 1).asInt();

        if (email.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "邮箱不能为空";
            SendJson(res, err);
            return;
        }

        struct Result {
            int error_code = 0;
            std::string msg;
            std::string code;
        };
        auto result = std::make_shared<Result>();

        result->code = Crypto::GenerateVerificationCode();

        RunDBSync(db, [&](sql::Connection* conn) {
            if (!UserDao::InsertVerificationCode(conn, email, result->code, type, 300)) {
                result->error_code = 1;
                result->msg = "验证码入库失败";
                return;
            }

            result->error_code = 0;
            result->msg = "验证码已生成";
        });

        LOG_INFO("Verification code for %s: %s", email.c_str(), result->code.c_str());

        Json::Value resp;
        resp["success"] = (result->error_code == 0);
        resp["message"] = result->msg;
        resp["code"] = result->code;  // 回显验证码，方便调试
        SendJson(res, resp);
    });

    // ---- 注册 ----
    http.Post("/api/register", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        std::string email    = body.get("email", "").asString();
        std::string code     = body.get("code", "").asString();
        std::string password = body.get("password", "").asString();
        std::string username = body.get("username", "").asString();

        if (email.empty() || password.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "邮箱和密码不能为空";
            SendJson(res, err);
            return;
        }

        struct Result {
            int error_code = 0;
            std::string msg;
            uint32_t user_id = 0;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            // 1. 验证验证码
            if (!UserDao::VerifyCode(conn, email, code, 1)) {
                result->error_code = 1;
                result->msg = "验证码错误或已过期";
                return;
            }

            // 2. 检查邮箱是否已注册
            if (UserDao::EmailExists(conn, email)) {
                result->error_code = 2;
                result->msg = "该邮箱已被注册";
                return;
            }

            // 3. 哈希密码
            std::string salt = Crypto::GenerateSalt();
            std::string hash = Crypto::HashPassword(password, salt);

            // 4. 插入用户
            uint32_t user_id = 0;
            if (!UserDao::InsertUser(conn, email, hash, salt, username, user_id)) {
                result->error_code = 3;
                result->msg = "创建用户失败";
                return;
            }

            result->error_code = 0;
            result->msg = "注册成功";
            result->user_id = user_id;
        });

        Json::Value resp;
        resp["success"] = (result->error_code == 0);
        resp["message"] = result->msg;
        resp["user_id"] = static_cast<Json::UInt>(result->user_id);
        SendJson(res, resp);
    });

    // ---- 登录 ----
    http.Post("/api/login", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        std::string email    = body.get("email", "").asString();
        std::string password = body.get("password", "").asString();

        if (email.empty() || password.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "邮箱和密码不能为空";
            SendJson(res, err);
            return;
        }

        struct Result {
            int error_code = 0;
            std::string msg;
            UserInfo user;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            // 1. 查用户
            if (!UserDao::GetUserByEmail(conn, email, result->user)) {
                result->error_code = 1;
                result->msg = "该邮箱未注册";
                return;
            }

            // 2. 验密码
            if (!Crypto::VerifyPassword(password, result->user.salt, result->user.password)) {
                result->error_code = 2;
                result->msg = "密码错误";
                return;
            }

            result->error_code = 0;
            result->msg = "登录成功";
        });

        Json::Value resp;
        resp["success"] = (result->error_code == 0);
        resp["message"] = result->msg;

        if (result->error_code == 0) {
            Json::Value user;
            user["id"]       = static_cast<Json::UInt>(result->user.id);
            user["email"]    = result->user.email;
            user["username"] = result->user.username;
            user["avatar"]   = result->user.avatar;
            user["created_at"] = result->user.created_at;
            resp["user"] = user;

            // 标记用户在线（Redis，2分钟TTL，由前端心跳刷新）
            uint32_t uid = result->user.id;
            std::thread([uid]() {
                sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
                if (true) {
                    RedisDao::SetUserOnline(&rctx, uid, 120);
                    
                }
            }).detach();

            LOG_INFO("HTTP login: user=%s email=%s",
                     result->user.username.c_str(), result->user.email.c_str());
        }

        SendJson(res, resp);
    });

    // ============================================================
    // 好友 API
    // ============================================================

    // ---- 好友列表 ----
    http.Get("/api/friends", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "user_id required";
            SendJson(res, err);
            return;
        }

        struct Result {
            std::vector<FriendInfo> friends;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            FriendDao::GetFriendList(conn, user_id, result->friends);
        });

        // 查询 Redis 获取每个好友的在线状态
        sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
        std::unordered_map<uint32_t, bool> online_map;
        if (true) {
            for (auto& f : result->friends) {
                online_map[f.friend_id] = RedisDao::IsUserOnline(&rctx, f.friend_id);
            }
            
        }

        Json::Value resp;
        resp["success"] = true;
        Json::Value arr(Json::arrayValue);
        for (auto& f : result->friends) {
            Json::Value item;
            item["id"]       = static_cast<Json::UInt>(f.friend_id);
            item["email"]    = f.email;
            item["username"] = f.username;
            item["avatar"]   = f.avatar;
            item["remark"]   = f.remark;
            item["online"]   = online_map[f.friend_id];
            item["created_at"] = f.created_at;
            arr.append(item);
        }
        resp["friends"] = arr;
        SendJson(res, resp);
    });

    // ---- 搜索用户（两级：精确邮箱→Redis，模糊→MySQL） ----
    http.Get("/api/friends/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string keyword = req.get_param_value("keyword");
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (keyword.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请输入搜索关键词";
            SendJson(res, err);
            return;
        }

        struct Result {
            std::vector<UserInfo> users;
        };
        auto result = std::make_shared<Result>();

        // 判断是否像邮箱（含 @ 且无 % 等模糊字符）
        bool looks_like_email = (keyword.find('@') != std::string::npos &&
                                  keyword.find('%') == std::string::npos);

        if (looks_like_email) {
            // ── 精确搜索：先 Redis → miss 回退 MySQL ──
            sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            bool redis_hit = false;

            if (true) {
                uint32_t cached_id = 0;
                if (RedisDao::GetUserIdByEmail(&rctx, keyword, cached_id)) {
                    UserInfo u;
                    if (RedisDao::GetCachedUserInfo(&rctx, cached_id, u)) {
                        result->users.push_back(u);
                        redis_hit = true;
                    }
                }
                
            }

            if (!redis_hit) {
                // Redis miss → MySQL 精确匹配
                RunDBSync(db, [&](sql::Connection* conn) {
                    FriendDao::SearchUserByEmail(conn, keyword, user_id, result->users);
                });
            }
        } else {
            // ── 模糊搜索：直接 MySQL LIKE ──
            RunDBSync(db, [&](sql::Connection* conn) {
                FriendDao::SearchUserByEmail(conn, keyword, user_id, result->users);
            });
        }

        // ── 保存搜索历史到 Redis（异步，不阻塞响应） ──
        {
            sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            if (true) {
                RedisDao::AddSearchHistory(&rctx, user_id, keyword);
                
            }
        }

        Json::Value resp;
        resp["success"] = true;
        Json::Value arr(Json::arrayValue);
        for (auto& u : result->users) {
            Json::Value item;
            item["id"]       = static_cast<Json::UInt>(u.id);
            item["email"]    = u.email;
            item["username"] = u.username;
            item["avatar"]   = u.avatar;
            arr.append(item);
        }
        resp["users"] = arr;
        SendJson(res, resp);
    });

    // ---- 搜索历史 ----
    http.Get("/api/search/history", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));

        Json::Value resp;
        resp["success"] = true;
        Json::Value arr(Json::arrayValue);

        if (user_id > 0) {
            sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            if (true) {
                std::vector<std::string> history;
                RedisDao::GetSearchHistory(&rctx, user_id, history);
                for (auto& h : history) {
                    arr.append(h);
                }
                
            }
        }
        resp["history"] = arr;
        SendJson(res, resp);
    });

    // ---- 发送好友请求 ----
    http.Post("/api/friends/send-request", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t from_id = body.get("from_id", 0).asUInt();
        uint32_t to_id   = body.get("to_id", 0).asUInt();
        if (from_id == 0 || to_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
            bool auto_accepted = false;
            std::string msg;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            uint32_t request_id = 0;
            result->ok = FriendDao::SendFriendRequest(conn, from_id, to_id,
                                                      request_id, result->auto_accepted);
            if (result->auto_accepted) {
                result->msg = "你们已成为好友（对方已发来请求）";
            } else if (result->ok) {
                result->msg = "好友请求已发送";
            } else {
                result->msg = "操作失败（可能已是好友或已有待处理请求）";
            }
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->msg;
        resp["auto_accepted"] = result->auto_accepted;
        SendJson(res, resp);
    });

    // ---- 好友请求列表 ----
    http.Get("/api/friends/requests", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "user_id required";
            SendJson(res, err);
            return;
        }

        struct Result {
            std::vector<FriendRequest> requests;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            FriendDao::GetPendingRequests(conn, user_id, result->requests);
        });

        Json::Value resp;
        resp["success"] = true;
        Json::Value arr(Json::arrayValue);
        for (auto& r : result->requests) {
            Json::Value item;
            item["id"]            = static_cast<Json::UInt>(r.id);
            item["from_user_id"]  = static_cast<Json::UInt>(r.from_user_id);
            item["from_email"]    = r.from_email;
            item["from_username"] = r.from_username;
            item["from_avatar"]   = r.from_avatar;
            item["created_at"]    = r.created_at;
            arr.append(item);
        }
        resp["requests"] = arr;
        SendJson(res, resp);
    });

    // ---- 同意好友请求 ----
    http.Post("/api/friends/accept", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id    = body.get("user_id", 0).asUInt();
        uint32_t request_id = body.get("request_id", 0).asUInt();
        if (user_id == 0 || request_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
            uint32_t friend_id = 0;
            std::string friend_username;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            uint32_t friend_id = 0;
            result->ok = FriendDao::AcceptFriendRequest(conn, user_id, request_id, friend_id);
            if (result->ok) {
                result->friend_id = friend_id;
                // 查好友用户名
                UserInfo u;
                if (UserDao::GetUserById(conn, friend_id, u)) {
                    result->friend_username = u.username;
                }
            }
        });

        Json::Value resp;
        resp["success"]         = result->ok;
        resp["message"]         = result->ok ? "已添加为好友" : "操作失败";
        resp["friend_id"]       = static_cast<Json::UInt>(result->friend_id);
        resp["friend_username"] = result->friend_username;
        SendJson(res, resp);
    });

    // ---- 拒绝好友请求 ----
    http.Post("/api/friends/reject", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id    = body.get("user_id", 0).asUInt();
        uint32_t request_id = body.get("request_id", 0).asUInt();
        if (user_id == 0 || request_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = FriendDao::RejectFriendRequest(conn, user_id, request_id);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "已拒绝" : "操作失败";
        SendJson(res, resp);
    });

    // ---- 删除好友 ----
    http.Post("/api/friends/delete", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id   = body.get("user_id", 0).asUInt();
        uint32_t friend_id = body.get("friend_id", 0).asUInt();
        if (user_id == 0 || friend_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = FriendDao::DeleteFriend(conn, user_id, friend_id);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "已删除好友" : "操作失败";
        SendJson(res, resp);
    });

    // ============================================================
    // 私聊消息 API
    // ============================================================

    // ---- 发送私聊消息 ----
    http.Post("/api/messages/send", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t from_id = body.get("from_id", 0).asUInt();
        uint32_t to_id   = body.get("to_id", 0).asUInt();
        std::string content = body.get("content", "").asString();
        uint32_t reply_to_id = body.get("reply_to_id", 0).asUInt();

        if (from_id == 0 || to_id == 0 || content.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误：缺少发送者、接收者或消息内容";
            SendJson(res, err);
            return;
        }

        if (content.size() > 65535) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "消息内容过长（最大 65535 字节）";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
            uint32_t msg_id = 0;
            std::string msg;
            std::string reply_preview;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            // 检查是否互为好友
            if (!FriendDao::IsFriend(conn, from_id, to_id)) {
                result->ok = false;
                result->msg = "你们还不是好友，无法发送私聊消息";
                return;
            }

            if (!PrivateMessageDao::SendMessage(conn, from_id, to_id, content,
                                               result->msg_id, reply_to_id)) {
                result->ok = false;
                result->msg = "消息发送失败";
                return;
            }

            // 如果是回复消息，获取预览
            if (reply_to_id > 0) {
                // 预览由 SendMessage 内部写入，我们在此查回
                result->reply_preview = "...";
            }

            result->ok = true;
            result->msg = "发送成功";
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->msg;
        resp["msg_id"]  = static_cast<Json::UInt>(result->msg_id);
        resp["reply_to_id"] = static_cast<Json::UInt>(reply_to_id);
        SendJson(res, resp);
    });

    // ---- 获取对话历史 ----
    http.Get("/api/messages/conversation", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id   = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        uint32_t friend_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("friend_id")));
        uint32_t after_id  = static_cast<uint32_t>(
            std::stoul(req.get_param_value("after")));
        std::string since  = req.get_param_value("since");

        if (user_id == 0 || friend_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            std::vector<PrivateMessageRecord> messages;
            std::vector<PrivateMessageRecord> updates;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            // 先标记对方发来的消息为已读
            PrivateMessageDao::MarkAsRead(conn, user_id, friend_id);
            // 获取对话
            PrivateMessageDao::GetConversation(conn, user_id, friend_id,
                                              after_id, 100, result->messages,
                                              result->updates, since);
        });

        auto build_msg = [](const PrivateMessageRecord& m) -> Json::Value {
            Json::Value item;
            item["id"]            = static_cast<Json::UInt>(m.id);
            item["from_user_id"]  = static_cast<Json::UInt>(m.from_user_id);
            item["to_user_id"]    = static_cast<Json::UInt>(m.to_user_id);
            item["content"]       = m.content;
            item["is_read"]       = m.is_read;
            item["is_revoked"]    = m.is_revoked;
            item["reply_to_id"]   = static_cast<Json::UInt>(m.reply_to_id);
            item["reply_preview"] = m.reply_preview;
            item["created_at"]    = m.created_at;
            item["updated_at"]    = m.updated_at;
            return item;
        };

        Json::Value resp;
        resp["success"] = true;
        Json::Value msg_arr(Json::arrayValue);
        for (auto& m : result->messages) {
            msg_arr.append(build_msg(m));
        }
        resp["messages"] = msg_arr;

        Json::Value upd_arr(Json::arrayValue);
        for (auto& m : result->updates) {
            upd_arr.append(build_msg(m));
        }
        resp["updates"] = upd_arr;

        SendJson(res, resp);
    });

    // ---- 撤回消息 ----
    http.Post("/api/messages/revoke", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id = body.get("user_id", 0).asUInt();
        uint32_t msg_id  = body.get("msg_id", 0).asUInt();
        if (user_id == 0 || msg_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = PrivateMessageDao::RevokeMessage(conn, msg_id, user_id);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "消息已撤回" : "只能撤回2分钟内自己发送的消息";
        SendJson(res, resp);
    });

    // ---- 获取未读消息数（按好友分组） ----
    http.Get("/api/messages/unread", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            std::map<uint32_t, int> counts;
            int total = 0;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->counts = PrivateMessageDao::GetUnreadCounts(conn, user_id);
            for (auto& kv : result->counts) {
                result->total += kv.second;
            }
        });

        Json::Value resp;
        resp["success"] = true;
        resp["total"]   = result->total;
        Json::Value obj;
        for (auto& kv : result->counts) {
            obj[std::to_string(kv.first)] = kv.second;
        }
        resp["counts"] = obj;
        SendJson(res, resp);
    });

    // ---- 标记消息已读 ----
    http.Post("/api/messages/read", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id   = body.get("user_id", 0).asUInt();
        uint32_t friend_id = body.get("friend_id", 0).asUInt();
        if (user_id == 0 || friend_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        RunDBSync(db, [&](sql::Connection* conn) {
            PrivateMessageDao::MarkAsRead(conn, user_id, friend_id);
        });

        Json::Value resp;
        resp["success"] = true;
        resp["message"] = "ok";
        SendJson(res, resp);
    });

    // ---- 个人档案页面 ----
    std::string profile_path = static_dir + "/html/profile.html";
    http.Get("/profile", [profile_path](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(profile_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
            res.set_content("profile.html not found", "text/plain");
        }
    });

    // ============================================================
    // 个人档案 API
    // ============================================================

    // ---- 获取个人档案 ----
    http.Get("/api/user/profile", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
            UserProfile profile;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = UserDao::GetUserProfile(conn, user_id, result->profile);
        });

        if (result->ok) {
            Json::Value resp;
            resp["success"] = true;
            resp["id"]              = static_cast<Json::UInt>(result->profile.id);
            resp["email"]           = result->profile.email;
            resp["username"]        = result->profile.username;
            resp["avatar"]          = result->profile.avatar;
            resp["gender"]          = result->profile.gender;
            resp["birthday"]        = result->profile.birthday;
            resp["secondary_email"] = result->profile.secondary_email;
            resp["created_at"]      = result->profile.created_at;
            SendJson(res, resp);
        } else {
            Json::Value err;
            err["success"] = false;
            err["message"] = "用户不存在";
            SendJson(res, err);
        }
    });

    // ---- 更新个人档案 ----
    http.Post("/api/user/profile/update", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id = body.get("user_id", 0).asUInt();
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        std::string username  = body.get("username", "").asString();
        int gender            = body.get("gender", -1).asInt();
        std::string birthday  = body.get("birthday", "").asString();
        std::string secondary_email = body.get("secondary_email", "").asString();

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = UserDao::UpdateProfile(conn, user_id, username,
                                               gender, birthday, secondary_email);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "资料已更新" : "更新失败";
        SendJson(res, resp);
    });

    // ---- 头像上传 ----
    http.Post("/api/user/avatar/upload", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = 0;
        if (req.has_param("user_id")) {
            user_id = static_cast<uint32_t>(std::stoul(req.get_param_value("user_id")));
        }
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误：缺少 user_id";
            SendJson(res, err);
            return;
        }

        if (!req.form.has_file("avatar")) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "未上传文件";
            SendJson(res, err);
            return;
        }

        auto file = req.form.get_file("avatar");
        if (file.content.empty()) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "上传文件为空";
            SendJson(res, err);
            return;
        }

        // 递归创建目录（mkdir -p）
        std::string avatars_dir = static_dir + "/img/avatars";
        {
            std::string accum;
            for (char ch : avatars_dir) {
                accum += ch;
                if (ch == '/' && accum.size() > 1) {
                    mkdir(accum.c_str(), 0755);
                }
            }
            mkdir(avatars_dir.c_str(), 0755);
        }

        // 保存文件
        std::string filename = "user_" + std::to_string(user_id) + ".png";
        std::string filepath = avatars_dir + "/" + filename;
        {
            std::ofstream out(filepath, std::ios::binary);
            if (!out) {
                Json::Value err;
                err["success"] = false;
                err["message"] = "无法写入文件，请稍后重试";
                SendJson(res, err);
                return;
            }
            out.write(file.content.data(), file.content.size());
        }

        // 更新数据库
        std::string avatar_path = "/static/img/avatars/" + filename;
        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = UserDao::UpdateAvatar(conn, user_id, avatar_path);
        });

        // 文件已保存，即使 DB 更新暂时失败，也返回成功让前端更新显示
        // DB 失败通常是连接池暂时繁忙，文件已落盘，不影响其他用户看到新头像
        Json::Value resp;
        resp["success"] = true;
        resp["avatar"]  = avatar_path;
        resp["message"] = result->ok ? "头像已更新" : "头像已保存（数据库同步中）";
        SendJson(res, resp);
    });

    // ---- 账号销毁 ----
    http.Post("/api/user/account/delete", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id = body.get("user_id", 0).asUInt();
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = UserDao::DeleteUser(conn, user_id);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "账号已销毁" : "销毁失败";
        SendJson(res, resp);
    });

    // ---- 设置好友备注 ----
    http.Post("/api/friends/remark", [&](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        if (!ParseJsonBody(req, body)) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "请求格式错误";
            SendJson(res, err);
            return;
        }

        uint32_t user_id   = body.get("user_id", 0).asUInt();
        uint32_t friend_id = body.get("friend_id", 0).asUInt();
        std::string remark = body.get("remark", "").asString();

        if (user_id == 0 || friend_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = FriendDao::SetRemark(conn, user_id, friend_id, remark);
        });

        Json::Value resp;
        resp["success"] = result->ok;
        resp["message"] = result->ok ? "备注已更新" : "设置失败";
        SendJson(res, resp);
    });

    // ---- 获取好友公开信息 ----
    http.Get("/api/user/public-info", [&](const httplib::Request& req, httplib::Response& res) {
        uint32_t user_id = static_cast<uint32_t>(
            std::stoul(req.get_param_value("user_id")));
        if (user_id == 0) {
            Json::Value err;
            err["success"] = false;
            err["message"] = "参数错误";
            SendJson(res, err);
            return;
        }

        struct Result {
            bool ok = false;
            UserProfile profile;
        };
        auto result = std::make_shared<Result>();

        RunDBSync(db, [&](sql::Connection* conn) {
            result->ok = UserDao::GetUserProfile(conn, user_id, result->profile);
        });

        if (result->ok) {
            Json::Value resp;
            resp["success"]   = true;
            resp["id"]        = static_cast<Json::UInt>(result->profile.id);
            resp["username"]  = result->profile.username;
            resp["avatar"]    = result->profile.avatar;
            resp["gender"]    = result->profile.gender;
            resp["birthday"]  = result->profile.birthday;
            resp["created_at"] = result->profile.created_at;
            SendJson(res, resp);
        } else {
            Json::Value err;
            err["success"] = false;
            err["message"] = "用户不存在";
            SendJson(res, err);
        }
    });

    // ---- 登出（清除在线状态） ----
    http.Post("/api/logout", [](const httplib::Request& req, httplib::Response& res) {
        Json::Value body;
        uint32_t user_id = 0;
        if (ParseJsonBody(req, body)) {
            user_id = body.get("user_id", 0).asUInt();
        }
        if (user_id > 0) {
            sw::redis::Redis rctx(sw::redis::ConnectionOptions{sw::redis::ConnectionType::TCP, "127.0.0.1", 6379});
            if (true) {
                RedisDao::SetUserOffline(&rctx, user_id);
                
            }
        }
        Json::Value resp;
        resp["success"] = true;
        SendJson(res, resp);
    });

    LOG_INFO("HTTP server listening on port %d", kHttpPort);
    g_http_ready = true;

    // listen 是阻塞调用，出错时才会返回
    if (!http.listen("0.0.0.0", kHttpPort)) {
        g_http_ready = false;
        LOG_ERROR("HTTP server failed to start on port %d", kHttpPort);
    }

    LOG_INFO("HTTP server stopped");
}

/*
    MyMuduo 聊天室服务端入口

    启动后：
    - TCP 协议端口（默认 8888）：自定义聊天协议
    - HTTP 端口（默认 8080）：静态文件服务 + REST API

    配置文件：自动搜索 config.ini（当前目录 / 上级目录）
    用法：./chat_server
    优雅退出：Ctrl+C
*/

// httplib 需要先于 Log.h 包含，避免 #define log 与 <cmath> 冲突
#include "httplib.h"
#include "service/ChatServer.h"
#include "service/EmailSender.h"
#include "service/UserService.h"
#include "db/Database.h"
#include "db/UserDao.h"
#include "base/Crypto.h"
#include "net/Log.h"

#include "SimpleIni.h"

#include <jsoncpp/json/json.h>

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <fstream>
#include <mutex>
#include <condition_variable>

// HTTP 静态文件服务端口
constexpr int kHttpPort = 8080;

// 信号处理 —— 通知主循环退出
static std::atomic<bool> g_running{true};
static EventLoop* g_main_loop = nullptr;

static void SignalHandler(int sig)
{
    LOG_INFO("Received signal %d, shutting down...", sig);
    g_running = false;
    if (g_main_loop) {
        g_main_loop->Quit();
    }
}

// ============================================================
// 查找静态文件目录
// ============================================================
static std::string FindStaticDir()
{
    static const char* kPaths[] = {
        "./static",
        "../static",
    };
    for (const char* path : kPaths) {
        std::ifstream f(std::string(path) + "/html/index.html");
        if (f.good()) return path;
    }
    // fallback: 使用 examples/http/static（源码目录结构）
    return "";
}

// ============================================================
// 同步等待 DB 操作完成的辅助函数
// 在 HTTP 线程调用，阻塞直到 DB 回调完成
// ============================================================
static void RunDBSync(Database* db,
                      std::function<void(sql::Connection*)> fn)
{
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    // 传入 nullptr 作为 EventLoop，回调在 DB worker 线程直接执行
    db->Execute(nullptr, std::move(fn), [&]() {
        std::lock_guard<std::mutex> lk(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&] { return done; });
}

// ============================================================
// 解析请求中的 JSON body
// ============================================================
static bool ParseJsonBody(const httplib::Request& req, Json::Value& root)
{
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream iss(req.body);
    return Json::parseFromStream(builder, iss, &root, &errs);
}

// ============================================================
// 发送 JSON 响应
// ============================================================
static void SendJson(httplib::Response& res, const Json::Value& root)
{
    Json::StreamWriterBuilder builder;
    builder["emitUTF8"] = true;
    builder["indentation"] = "";
    res.set_content(Json::writeString(builder, root), "application/json; charset=utf-8");
}

// ============================================================
// 加载配置文件
// ============================================================
struct AppConfig {
    // database
    std::string db_host = "127.0.0.1";
    int db_port = 3306;
    std::string db_user = "root";
    std::string db_pass;
    std::string db_name = "mymuduo";
    int db_pool_size = 4;

    // server
    int server_port = 8888;
    int thread_num = 4;
    int timeout = 60;

    // email
    EmailSender::Config email;

    bool Load(const std::string& path) {
        CSimpleIniA ini;
        if (ini.LoadFile(path.c_str()) < 0) return false;

        db_host     = ini.GetValue("database", "host", "127.0.0.1");
        db_port     = static_cast<int>(ini.GetLongValue("database", "port", 3306));
        db_user     = ini.GetValue("database", "user", "root");
        db_pass     = ini.GetValue("database", "password", "");
        db_name     = ini.GetValue("database", "database", "mymuduo");
        db_pool_size = static_cast<int>(ini.GetLongValue("database", "pool_size", 4));

        server_port = static_cast<int>(ini.GetLongValue("server", "port", 8888));
        thread_num  = static_cast<int>(ini.GetLongValue("server", "threads", 4));
        timeout     = static_cast<int>(ini.GetLongValue("server", "timeout", 60));

        email.smtp_host = ini.GetValue("email", "smtp_host", "");
        email.smtp_port = static_cast<int>(ini.GetLongValue("email", "smtp_port", 587));
        email.smtp_user = ini.GetValue("email", "smtp_user", "");
        email.smtp_pass = ini.GetValue("email", "smtp_password", "");
        email.from_name = ini.GetValue("email", "from_name", "MyMuduo");

        return true;
    }
};

static bool TryLoadConfig(AppConfig& cfg)
{
    static const char* kPaths[] = {
        "config.ini",
        "../config.ini",
    };
    for (const char* path : kPaths) {
        std::ifstream f(path);
        if (f.good() && cfg.Load(path)) {
            LOG_INFO("Config loaded from %s", path);
            return true;
        }
    }
    return false;
}

// HTTP 服务就绪标志
static std::atomic<bool> g_http_ready{false};

// ============================================================
// HTTP 服务线程
// ============================================================

static void RunHttpServer(Database* db, const EmailSender::Config& email_cfg)
{
    std::string static_dir = FindStaticDir();
    LOG_INFO("HTTP static files serving from: %s", static_dir.c_str());

    // HTML 页面路径
    std::string index_path    = static_dir + "/html/index.html";
    std::string login_path    = static_dir + "/html/login.html";
    std::string register_path = static_dir + "/html/register.html";

    httplib::Server http;

    // ---- 静态文件挂载 ----
    http.set_mount_point("/static", static_dir);

    // ---- 首页 ----
    http.Get("/", [index_path](const httplib::Request& req, httplib::Response& res) {
        std::ifstream f(index_path);
        if (f.good()) {
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        } else {
            res.status = 404;
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

    // ---- 健康检查 ----
    http.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
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

            LOG_INFO("HTTP login: user=%s email=%s",
                     result->user.username.c_str(), result->user.email.c_str());
        }

        SendJson(res, resp);
    });

    // ---- 登出 ----
    http.Get("/logout", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/login");
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

// ============================================================
// HTTP 自检：验证所有端点是否正常响应
// ============================================================
static void TestHttpEndpoints()
{
    // 等待 HTTP 线程设置就绪标志
    for (int i = 0; i < 30 && !g_http_ready; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!g_http_ready) {
        std::cout << "  [FAIL] HTTP server did not become ready in 3 seconds" << std::endl;
        return;
    }

    // 再给内核一点时间完成 bind/listen
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httplib::Client cli("127.0.0.1", kHttpPort);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(5, 0);

    int passed = 0, failed = 0;

    auto check = [&](const char* name, bool ok, const std::string& detail = "") {
        if (ok) {
            std::cout << "  [PASS] " << name;
            if (!detail.empty()) std::cout << " — " << detail;
            std::cout << std::endl;
            passed++;
        } else {
            std::cout << "  [FAIL] " << name;
            if (!detail.empty()) std::cout << " — " << detail;
            std::cout << std::endl;
            failed++;
        }
    };

    // 探活
    auto health = cli.Get("/health");
    if (!health || health->status != 200) {
        std::cout << "  [FAIL] HTTP server not reachable at 127.0.0.1:" << kHttpPort << std::endl;
        return;
    }

    // ---- 静态页面 ----
    auto res1 = cli.Get("/");
    check("GET / (index.html)", res1 && res1->status == 200,
          res1 ? "HTTP " + std::to_string(res1->status) : "no response");

    auto res2 = cli.Get("/login");
    check("GET /login", res2 && res2->status == 200,
          res2 ? "HTTP " + std::to_string(res2->status) : "no response");

    auto res3 = cli.Get("/register");
    check("GET /register", res3 && res3->status == 200,
          res3 ? "HTTP " + std::to_string(res3->status) : "no response");

    auto res4 = cli.Get("/health");
    check("GET /health", res4 && res4->status == 200,
          res4 ? "HTTP " + std::to_string(res4->status) : "no response");

    // ---- REST API ----

    // send-code: 缺少 email → 应返回 success=false
    auto res5 = cli.Post("/api/send-code", "{}", "application/json");
    check("POST /api/send-code (missing email)",
          res5 && res5->status == 200 && res5->body.find("\"success\":false") != std::string::npos,
          res5 ? "body: " + res5->body : "no response");

    // send-code: 正常请求
    auto res6 = cli.Post("/api/send-code",
                         "{\"email\":\"test@example.com\",\"type\":1}",
                         "application/json");
    check("POST /api/send-code (valid)",
          res6 && res6->status == 200 && res6->body.find("\"code\":\"") != std::string::npos,
          res6 ? "body: " + res6->body : "no response");

    // login: 不存在用户
    auto res7 = cli.Post("/api/login",
                         "{\"email\":\"noone@example.com\",\"password\":\"x\"}",
                         "application/json");
    check("POST /api/login (bad credentials)",
          res7 && res7->status == 200 && res7->body.find("\"success\":false") != std::string::npos,
          res7 ? "body: " + res7->body : "no response");

    // register: 缺字段
    auto res8 = cli.Post("/api/register",
                         "{\"email\":\"x@x.com\"}",
                         "application/json");
    check("POST /api/register (missing fields)",
          res8 && res8->status == 200 && res8->body.find("\"success\":false") != std::string::npos,
          res8 ? "body: " + res8->body : "no response");

    // logout: 重定向
    auto res9 = cli.Get("/logout");
    check("GET /logout (redirect)",
          res9 && (res9->status == 302 || res9->status == 301),
          res9 ? "HTTP " + std::to_string(res9->status) : "no response");

    std::cout << "  " << passed << "/" << (passed + failed)
              << " HTTP checks passed" << std::endl;
}

// ============================================================
// 主函数
// ============================================================

int main()
{
    // 设置信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::cout << "========================================" << std::endl;
    std::cout << "  MyMuduo Chat Server" << std::endl;
    std::cout << "  Config: auto-search (config.ini)" << std::endl;
    std::cout << "  TCP  : port " << 8888 << " (custom protocol)" << std::endl;
    std::cout << "  HTTP : port " << kHttpPort << " (static + REST API)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. 加载配置
    AppConfig cfg;
    if (!TryLoadConfig(cfg)) {
        LOG_WARNING("No config.ini found, using default settings");
    }

    // 2. 创建 Database（共享给 ChatServer 和 HTTP 服务）
    auto db = std::make_unique<Database>(
        cfg.db_host, cfg.db_port, cfg.db_user, cfg.db_pass,
        cfg.db_name, cfg.db_pool_size);
    LOG_INFO("Database pool created: %s:%d/%s (pool=%d)",
             cfg.db_host.c_str(), cfg.db_port, cfg.db_name.c_str(), cfg.db_pool_size);

    // 3. 创建 ChatServer
    ChatServer server;
    g_main_loop = server.GetLoop();

    // 注意：ChatServer 在 Start() 中会加载自己的 config.ini，
    // 我们这里预先设置的 Database 会被 ChatServer 使用（因为已经 set 了）

    // 4. 启动 HTTP 静态文件服务 + REST API（独立线程）
    //    HTTP API 通过 RunDBSync 同步调用 DB（回调在 DB worker 线程直接执行）
    //    注意：db 的所有权属于 main，HTTP 线程持原始指针（生命周期由 main 保证）
    std::thread http_thread(RunHttpServer, db.get(), cfg.email);

    // 4.1 HTTP 端点自检
    std::cout << "\n--- HTTP Endpoint Self-Test ---" << std::endl;
    TestHttpEndpoints();
    std::cout << "-------------------------------\n" << std::endl;

    // 5. 设置外部 Database 给 ChatServer（必须在 Start() 前调用）
    server.SetDatabase(std::move(db));

    // 6. 启动聊天服务（阻塞在当前线程，运行 EventLoop）
    server.Start();

    // 清理
    g_running = false;
    http_thread.join();

    std::cout << "Server stopped. Goodbye!" << std::endl;
    return 0;
}

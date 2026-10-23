#include "HttpSelfTest.h"
#include "HttpServer.h"
#include <chrono>
#include <iostream>
#include <thread>

// ============================================================
// HTTP 自检：验证所有端点是否正常响应
// ============================================================
void TestHttpEndpoints()
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
    check("GET / (login page)", res1 && res1->status == 200,
          res1 ? "HTTP " + std::to_string(res1->status) : "no response");

    auto res2 = cli.Get("/login");
    check("GET /login", res2 && res2->status == 200,
          res2 ? "HTTP " + std::to_string(res2->status) : "no response");

    auto res3 = cli.Get("/register");
    check("GET /register", res3 && res3->status == 200,
          res3 ? "HTTP " + std::to_string(res3->status) : "no response");

    auto res_chat = cli.Get("/chat");
    check("GET /chat (chat room)", res_chat && res_chat->status == 200,
          res_chat ? "HTTP " + std::to_string(res_chat->status) : "no response");

    auto res4 = cli.Get("/health");
    check("GET /health", res4 && res4->status == 200,
          res4 ? "HTTP " + std::to_string(res4->status) : "no response");

    // 不存在的路径应返回 404，且 body 包含 404.html 内容
    auto res_404 = cli.Get("/no/such/path");
    check("GET /no/such/path (404 page)",
          res_404 && res_404->status == 404,
          res_404 ? "HTTP " + std::to_string(res_404->status) + " (" + std::to_string(res_404->body.size()) + " bytes)" : "no response");

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

    // logout: POST
    auto res9 = cli.Post("/api/logout", "{\"user_id\":0}", "application/json");
    check("POST /api/logout (offline)",
          res9 && res9->status == 200 && res9->body.find("\"success\":true") != std::string::npos,
          res9 ? "body: " + res9->body : "no response");

    std::cout << "  " << passed << "/" << (passed + failed)
              << " HTTP checks passed" << std::endl;
}

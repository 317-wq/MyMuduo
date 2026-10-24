// MyMuduo 聊天室服务端入口。
#include "AppConfig.h"
#include "HttpServer.h"
#include "HttpSelfTest.h"
#include "service/ChatServer.h"
#include "db/Database.h"
#include "net/Log.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

static std::atomic<bool> g_running{true};
static EventLoop* g_main_loop = nullptr;

// 处理终止信号并通知主事件循环退出。
static void SignalHandler(int sig)
{
    LOG_INFO("Received signal %d, shutting down...", sig);
    g_running = false;
    if (g_main_loop) g_main_loop->Quit();
}

int main()
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
    std::cout << "========================================" << std::endl;
    std::cout << "  MyMuduo Chat Server" << std::endl;
    std::cout << "  Config: auto-search (config.ini)" << std::endl;
    std::cout << "  TCP  : port " << 8888 << " (custom protocol)" << std::endl;
    std::cout << "  HTTP : port " << kHttpPort << " (static + REST API)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 加载配置文件信息
    AppConfig cfg;
    if (!TryLoadConfig(cfg)) LOG_WARNING("No config.ini found, using default settings");

    // 配置mysql数据库[包含连接池]
    auto db = std::make_unique<Database>(cfg.db_host, cfg.db_port, cfg.db_user, cfg.db_pass, cfg.db_name, cfg.db_pool_size);
    LOG_INFO("Database pool created: %s:%d/%s (pool=%d)", cfg.db_host.c_str(), cfg.db_port, cfg.db_name.c_str(), cfg.db_pool_size);

    // 主reactor[main thread]
    ChatServer server;
    g_main_loop = server.GetLoop();

    std::thread http_thread(RunHttpServer, db.get(), cfg.email);
    std::cout << "\n--- HTTP Endpoint Self-Test ---" << std::endl;

    TestHttpEndpoints();
    std::cout << "-------------------------------\n" << std::endl;
    
    server.SetDatabase(std::move(db));
    server.Start();

    // base_loop 已退出，通知 HTTP 线程停止并等待其释放端口。
    StopHttpServer();
    g_running = false;
    http_thread.join();
    std::cout << "Server stopped. Goodbye!" << std::endl;
    return 0;
}

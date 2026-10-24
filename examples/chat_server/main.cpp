// MyMuduo 纯 TCP 聊天室服务端入口。
#include "AppConfig.h"
#include "service/ChatServer.h"
#include "db/Database.h"
#include "net/Log.h"
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>

static EventLoop* g_main_loop = nullptr;

// 处理终止信号并通知主事件循环退出。
static void SignalHandler(int sig)
{
    LOG_INFO("Received signal %d, shutting down...", sig);
    if (g_main_loop) g_main_loop->Quit();
}

int main()
{
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    std::cout << "========================================" << std::endl;
    std::cout << "  MyMuduo TCP Chat Server" << std::endl;
    std::cout << "  Config: auto-search (config.ini)" << std::endl;
    std::cout << "  TCP   : port 8888 (custom protocol)" << std::endl;
    std::cout << "========================================" << std::endl;

    // 加载配置文件信息
    AppConfig cfg;
    if (!TryLoadConfig(cfg)) LOG_WARNING("No config.ini found, using default settings");

    // 配置 MySQL 数据库[包含连接池]
    auto db = std::make_unique<Database>(
        cfg.db_host, cfg.db_port, cfg.db_user, cfg.db_pass,
        cfg.db_name, cfg.db_pool_size);
    LOG_INFO("Database pool created: %s:%d/%s (pool=%d)",
             cfg.db_host.c_str(), cfg.db_port, cfg.db_name.c_str(), cfg.db_pool_size);

    // 主 Reactor[main thread]
    ChatServer server;
    g_main_loop = server.GetLoop();
    server.SetDatabase(std::move(db));

    int exit_code = 0;
    try {
        server.Start();
    } catch (const std::exception& e) {
        // 端口占用、timeout=0 等启动错误：记录后退出，不能假装启动成功。
        LOG_ERROR("Server start failed: %s", e.what());
        exit_code = 1;
    }

    std::cout << (exit_code == 0 ? "Server stopped. Goodbye!"
                                 : "Server failed to start.")
              << std::endl;
    return exit_code;
}

#pragma once

#include "httplib.h"
#include "service/EmailSender.h"
#include "db/Database.h"
#include <atomic>

// HTTP 服务端口。
constexpr int kHttpPort = 8080;
// HTTP 服务就绪标志。
extern std::atomic<bool> g_http_ready;
// 启动 HTTP 静态文件服务和 REST API。
void RunHttpServer(Database* db, const EmailSender::Config& email_cfg);

// 停止 HTTP 服务并唤醒阻塞中的 listen，供进程退出流程调用。
void StopHttpServer();

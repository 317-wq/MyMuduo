#pragma once

/*
    ChatServer — 聊天室服务主控类

    职责：
    1. 加载配置（SimpleIni）
    2. 初始化 Database、UserService、TcpServer、Codec、Dispatcher
    3. 注册所有消息处理器（登录/注册/验证码/登出/心跳）
    4. 管理在线用户状态（user_id ↔ TcpConnection 映射）
    5. 提供 Start() / Stop() 生命周期

    线程模型：
    - 主线程（base_loop）: Acceptor、TimeWheel、Dispatcher 回调
    - IO 线程池: TcpConnection 的读写、Codec 解析
    - DB 线程池: MySQL 查询

    回调链路：
    TcpServer::OnMessage(raw Buffer)
      → Codec::OnMessage(conn, buf)        // IO 线程
        → 解出完整帧 → MessageCallback
          → Dispatcher::Dispatch(conn, msg) // IO 线程
            → handler(conn, msg, ts)       // IO 线程
              → UserService::Login()       // 投递到 DB 线程
                → 回调回到 IO 线程
                  → conn->Send(response)   // IO 线程
*/

#include "base/NoCopy.h"
#include "db/Database.h"
#include "service/UserService.h"
#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "proto/Codec.h"
#include "proto/Dispatcher.h"
#include "proto/Message.h"

#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>
#include <cstdint>

class ChatServer : NoCopy {
public:
    ChatServer();
    ~ChatServer();

    // 自动搜索并加载 config.ini，然后启动服务（阻塞在当前线程，运行 EventLoop）
    void Start();

    // 停止服务
    void Stop();

    // 获取 EventLoop 指针（供 HTTP 服务器等外部组件投递任务）
    EventLoop* GetLoop() const { return _base_loop; }

    // 设置外部创建的 Database（必须在 Start() 前调用）
    // 如果不调用，Start() 会自行创建
    void SetDatabase(std::unique_ptr<Database> db) { _db = std::move(db); }

private:
    // ---------- 配置 ----------
    void LoadConfig(const std::string& path);
    bool TryLoadConfig();  // 尝试多个路径加载 config.ini

    // ---------- TcpServer 回调 ----------
    void OnConnection(const TcpConnection::Ptr& conn);
    void OnRawMessage(const TcpConnection::Ptr& conn, Buffer* buf);

    // ---------- Dispatcher 处理器 ----------
    void OnSendCode(const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts);
    void OnRegister(const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts);
    void OnLogin(const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts);
    void OnLogout(const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts);
    void OnHeartbeat(const TcpConnection::Ptr& conn, Message::Ptr msg, Timestamp ts);

    // ---------- 在线用户管理 ----------
    void SetUserOnline(uint32_t user_id, const TcpConnection::Ptr& conn);
    void SetUserOffline(uint32_t user_id);
    TcpConnection::Ptr GetConnectionByUserId(uint32_t user_id);

    // ---------- 工具 ----------
    void SendMessage(const TcpConnection::Ptr& conn, const Message& msg);

    // ---------- 组件 ----------
    EventLoop* _base_loop = nullptr;   // 不持有所有权，new/delete 管理
    std::unique_ptr<Database> _db;
    std::unique_ptr<UserService> _user_service;
    std::unique_ptr<TcpServer> _server;
    Codec _codec;
    Dispatcher _dispatcher;

    // 在线用户
    std::mutex _online_mutex;
    // user_id → TcpConnection（弱引用，连接断开后自动失效）
    std::unordered_map<uint32_t, TcpConnection::Ptr> _online_users;

    // 配置参数
    std::string _db_host = "127.0.0.1";
    int _db_port = 3306;
    std::string _db_user = "root";
    std::string _db_pass;
    std::string _db_name = "mymuduo";
    int _db_pool_size = 4;
    int _server_port = 8888;
    int _thread_num = 4;
    int _timeout = 60;

    // 邮件配置
    EmailSender::Config _email_cfg;
};

#pragma once

#include "service/EmailSender.h"
#include <string>

// 应用程序配置及配置文件加载接口。
struct AppConfig {
    // 数据库连接主机。
    std::string db_host = "127.0.0.1";
    // 数据库连接端口。
    int db_port = 3306;
    // 数据库用户名。
    std::string db_user = "root";
    // 数据库密码。
    std::string db_pass;
    // 数据库名称。
    std::string db_name = "mymuduo";
    // 数据库连接池大小。
    int db_pool_size = 4;
    // 聊天服务端口。
    int server_port = 8888;
    // 聊天服务线程数。
    int thread_num = 4;
    // 服务超时时间。
    int timeout = 60;
    // 邮件发送配置。
    EmailSender::Config email;
    // 从指定文件加载应用配置。
    bool Load(const std::string& path);
};

// 自动搜索并加载应用配置文件。
bool TryLoadConfig(AppConfig& cfg);

#include "AppConfig.h"
#include "SimpleIni.h"
#include "net/Log.h"
#include <fstream>

bool AppConfig::Load(const std::string& path) {
    CSimpleIniA ini;
    if (ini.LoadFile(path.c_str()) < 0) return false;
    db_host = ini.GetValue("database", "host", "127.0.0.1");
    db_port = static_cast<int>(ini.GetLongValue("database", "port", 3306));
    db_user = ini.GetValue("database", "user", "root");
    db_pass = ini.GetValue("database", "password", "");
    db_name = ini.GetValue("database", "database", "mymuduo");
    db_pool_size = static_cast<int>(ini.GetLongValue("database", "pool_size", 4));
    server_port = static_cast<int>(ini.GetLongValue("server", "port", 8888));
    thread_num = static_cast<int>(ini.GetLongValue("server", "threads", 4));
    timeout = static_cast<int>(ini.GetLongValue("server", "timeout", 60));
    email.smtp_host = ini.GetValue("email", "smtp_host", "");
    email.smtp_port = static_cast<int>(ini.GetLongValue("email", "smtp_port", 587));
    email.smtp_user = ini.GetValue("email", "smtp_user", "");
    email.smtp_pass = ini.GetValue("email", "smtp_password", "");
    email.from_name = ini.GetValue("email", "from_name", "MyMuduo");
    return true;
}

bool TryLoadConfig(AppConfig& cfg) {
    static const char* kPaths[] = {"config.ini", "../config.ini"};
    for (const char* path : kPaths) {
        std::ifstream f(path);
        if (f.good() && cfg.Load(path)) {
            LOG_INFO("Config loaded from %s", path);
            return true;
        }
    }
    return false;
}

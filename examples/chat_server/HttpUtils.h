#pragma once

#include "httplib.h"
#include "db/Database.h"
#include <functional>
#include <string>
#include <jsoncpp/json/json.h>

// 查找静态页面目录。
std::string FindStaticDir();
// 同步执行数据库任务并等待完成。
void RunDBSync(Database* db, std::function<void(sql::Connection*)> fn);
// 解析 HTTP 请求中的 JSON body。
bool ParseJsonBody(const httplib::Request& req, Json::Value& root);
// 发送 JSON 响应。
void SendJson(httplib::Response& res, const Json::Value& root);

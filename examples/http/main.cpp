/*
    HTTP 服务端示例 — 登录 + 注册 + 首页

    演示如何在 TcpServer 之上构建 HTTP 协议支持：
    - HTTP 请求解析（请求行 + 头部 + Body）
    - HTTP 响应构建（状态码 + 头部 + 重定向）
    - 表单 POST 解析（application/x-www-form-urlencoded）
    - Cookie/Session 认证
    - 用户注册与登录
    - 三个静态 HTML 页面内嵌

    用法：
        ./http_server [port]
        浏览器访问 http://localhost:8888/
*/

#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "net/Buffer.h"
#include "net/TcpConnection.h"
#include "net/InetAddress.h"

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <random>
#include <mutex>
#include <regex>

// ============================================================
// 1. HTTP 请求解析
// ============================================================
struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> query_params;
    std::string body;

    void Clear() {
        method.clear();
        path.clear();
        headers.clear();
        query_params.clear();
        body.clear();
    }
};

// URL 解码 (%XX → 字符)
static std::string UrlDecode(const std::string& src) {
    std::string result;
    result.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()) {
            int hi = src[i + 1];
            int lo = src[i + 2];
            auto hex = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex(hi), l = hex(lo);
            if (h >= 0 && l >= 0) {
                result += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        } else if (src[i] == '+') {
            result += ' ';
            continue;
        }
        result += src[i];
    }
    return result;
}

// 解析 URL 查询参数 ?key=val&key2=val2
static void ParseQueryString(const std::string& qs,
                             std::unordered_map<std::string, std::string>& out) {
    std::istringstream iss(qs);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos)
            out[UrlDecode(pair.substr(0, pos))] = UrlDecode(pair.substr(pos + 1));
        else
            out[UrlDecode(pair)] = "";
    }
}

// 解析原始 HTTP 报文
static bool ParseHttpRequest(const std::string& raw, HttpRequest& req) {
    req.Clear();

    std::istringstream stream(raw);
    std::string line;

    // — 请求行 —
    if (!std::getline(stream, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    {
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path;
        if (req.method.empty() || req.path.empty()) return false;

        auto qpos = req.path.find('?');
        if (qpos != std::string::npos) {
            ParseQueryString(req.path.substr(qpos + 1), req.query_params);
            req.path = req.path.substr(0, qpos);
        }
    }

    // — 头部 —
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        auto pos = line.find(':');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            size_t start = 0;
            while (start < val.size() && (val[start] == ' ' || val[start] == '\t'))
                ++start;
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            req.headers[key] = val.substr(start);
        }
    }

    // — Body —
    auto it = req.headers.find("content-length");
    if (it != req.headers.end()) {
        int len = std::stoi(it->second);
        if (len > 0) {
            std::string remaining;
            auto current = stream.tellg();
            if (current >= 0) {
                remaining = raw.substr(static_cast<size_t>(current));
            }
            if (remaining.size() < static_cast<size_t>(len)) {
                return false;
            }
            req.body = remaining.substr(0, len);
        }
    }

    return true;
}

// ============================================================
// 2. HTTP 响应构建
// ============================================================
struct HttpResponse {
    int status_code = 200;
    std::string status_msg = "OK";
    std::string content_type = "text/html; charset=utf-8";
    std::string body;
    std::string location;
    std::string set_cookie;

    std::string ToString() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << status_msg << "\r\n";
        oss << "Content-Type: " << content_type << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        if (!location.empty())
            oss << "Location: " << location << "\r\n";
        if (!set_cookie.empty())
            oss << "Set-Cookie: " << set_cookie << "\r\n";
        oss << "Connection: close\r\n";
        oss << "Server: MyMuduo/1.0\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    static HttpResponse Ok(const std::string& html) {
        HttpResponse resp;
        resp.status_code = 200;
        resp.body = html;
        return resp;
    }

    static HttpResponse Redirect(const std::string& location) {
        HttpResponse resp;
        resp.status_code = 302;
        resp.status_msg = "Found";
        resp.location = location;
        return resp;
    }

    static HttpResponse NotFound() {
        HttpResponse resp;
        resp.status_code = 404;
        resp.status_msg = "Not Found";
        resp.body = R"(<html><head><meta charset="UTF-8"></head><body><h1>404 Not Found</h1><p>页面未找到</p></body></html>)";
        return resp;
    }
};

// ============================================================
// 3. HTML 页面（内嵌）
// ============================================================

// --- 登录页面 ---
static const char* kLoginPage = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyChat - 登录</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 48px 40px;
            width: 420px;
            max-width: 90vw;
        }
        .logo { text-align: center; font-size: 48px; margin-bottom: 8px; }
        h1 { text-align: center; color: #333; margin-bottom: 8px; font-size: 24px; }
        .subtitle { text-align: center; color: #999; font-size: 14px; margin-bottom: 32px; }
        .form-group { margin-bottom: 20px; }
        label { display: block; color: #555; font-size: 14px; margin-bottom: 6px; font-weight: 500; }
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 12px 16px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 15px;
            transition: border-color 0.3s, box-shadow 0.3s;
            outline: none;
        }
        input[type="text"]:focus,
        input[type="password"]:focus {
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.15);
        }
        .remember-row {
            display: flex;
            align-items: center;
            gap: 8px;
            margin-bottom: 20px;
            font-size: 14px;
            color: #666;
        }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover {
            transform: translateY(-1px);
            box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
        }
        .error {
            background: #fff5f5;
            color: #e53e3e;
            text-align: center;
            padding: 10px;
            border-radius: 6px;
            margin-bottom: 16px;
            font-size: 14px;
            border: 1px solid #fed7d7;
        }
        .success {
            background: #f0fff4;
            color: #38a169;
            text-align: center;
            padding: 10px;
            border-radius: 6px;
            margin-bottom: 16px;
            font-size: 14px;
            border: 1px solid #c6f6d5;
        }
        .footer { text-align: center; color: #999; font-size: 13px; margin-top: 24px; }
        .footer a { color: #667eea; text-decoration: none; font-weight: 500; }
        .footer a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">💬</div>
        <h1>MyChat 聊天室</h1>
        <p class="subtitle">登录你的账号，开始聊天</p>
        {{MESSAGE}}
        <form method="POST" action="/login">
            <div class="form-group">
                <label for="username">用户名</label>
                <input type="text" id="username" name="username"
                       placeholder="请输入用户名" required autofocus autocomplete="username">
            </div>
            <div class="form-group">
                <label for="password">密码</label>
                <input type="password" id="password" name="password"
                       placeholder="请输入密码" required autocomplete="current-password">
            </div>
            <div class="remember-row">
                <input type="checkbox" id="remember" name="remember" value="1">
                <label for="remember" style="margin-bottom:0; cursor:pointer;">记住我</label>
            </div>
            <button type="submit">登 录</button>
        </form>
        <div class="footer">
            还没有账号？<a href="/register">立即注册</a>
        </div>
    </div>
</body>
</html>
)html";

// --- 注册页面 ---
static const char* kRegisterPage = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyChat - 注册</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;
            background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.25);
            padding: 48px 40px;
            width: 420px;
            max-width: 90vw;
        }
        .logo { text-align: center; font-size: 48px; margin-bottom: 8px; }
        h1 { text-align: center; color: #333; margin-bottom: 8px; font-size: 24px; }
        .subtitle { text-align: center; color: #999; font-size: 14px; margin-bottom: 32px; }
        .form-group { margin-bottom: 20px; }
        label { display: block; color: #555; font-size: 14px; margin-bottom: 6px; font-weight: 500; }
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 12px 16px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 15px;
            transition: border-color 0.3s, box-shadow 0.3s;
            outline: none;
        }
        input[type="text"]:focus,
        input[type="password"]:focus {
            border-color: #11998e;
            box-shadow: 0 0 0 3px rgba(17, 153, 142, 0.15);
        }
        .password-hint { font-size: 12px; color: #999; margin-top: 4px; }
        button {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        button:hover {
            transform: translateY(-1px);
            box-shadow: 0 4px 15px rgba(17, 153, 142, 0.4);
        }
        .error {
            background: #fff5f5;
            color: #e53e3e;
            text-align: center;
            padding: 10px;
            border-radius: 6px;
            margin-bottom: 16px;
            font-size: 14px;
            border: 1px solid #fed7d7;
        }
        .footer { text-align: center; color: #999; font-size: 13px; margin-top: 24px; }
        .footer a { color: #11998e; text-decoration: none; font-weight: 500; }
        .footer a:hover { text-decoration: underline; }
        .terms {
            font-size: 12px;
            color: #999;
            text-align: center;
            margin-top: 16px;
            line-height: 1.6;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">✨</div>
        <h1>创建账号</h1>
        <p class="subtitle">注册 MyChat 账号，加入聊天</p>
        {{MESSAGE}}
        <form method="POST" action="/register">
            <div class="form-group">
                <label for="username">用户名</label>
                <input type="text" id="username" name="username"
                       placeholder="4-20 位字母、数字或下划线" required autofocus
                       autocomplete="username">
            </div>
            <div class="form-group">
                <label for="password">密码</label>
                <input type="password" id="password" name="password"
                       placeholder="至少 6 位字符" required
                       autocomplete="new-password">
                <div class="password-hint">至少 6 位字符</div>
            </div>
            <div class="form-group">
                <label for="confirm_password">确认密码</label>
                <input type="password" id="confirm_password" name="confirm_password"
                       placeholder="请再次输入密码" required
                       autocomplete="new-password">
            </div>
            <button type="submit">注 册</button>
        </form>
        <div class="terms">
            注册即表示同意服务条款和隐私政策
        </div>
        <div class="footer">
            已有账号？<a href="/login">立即登录</a>
        </div>
    </div>
</body>
</html>
)html";

// --- 首页 ---
static const char* kHomePage = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyChat - 首页</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', sans-serif;
            background: #f0f2f5;
            min-height: 100vh;
        }
        .navbar {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 0 32px;
            height: 60px;
            display: flex;
            align-items: center;
            justify-content: space-between;
            box-shadow: 0 2px 10px rgba(0,0,0,0.15);
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .navbar .brand {
            font-size: 20px;
            font-weight: 700;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .navbar .nav-links { display: flex; align-items: center; gap: 24px; }
        .navbar .nav-links a {
            color: rgba(255,255,255,0.85);
            text-decoration: none;
            font-size: 14px;
            transition: color 0.2s;
        }
        .navbar .nav-links a:hover { color: white; }
        .navbar .user-menu { display: flex; align-items: center; gap: 16px; }
        .navbar .username { font-size: 14px; opacity: 0.9; }
        .navbar .avatar {
            width: 36px;
            height: 36px;
            border-radius: 50%;
            background: rgba(255,255,255,0.25);
            display: flex;
            align-items: center;
            justify-content: center;
            font-weight: 700;
            font-size: 16px;
        }
        .logout-btn {
            background: rgba(255,255,255,0.15);
            color: white;
            border: 1px solid rgba(255,255,255,0.25);
            padding: 8px 20px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 13px;
            text-decoration: none;
            transition: background 0.3s;
        }
        .logout-btn:hover { background: rgba(255,255,255,0.28); }
        .content { max-width: 960px; margin: 0 auto; padding: 32px 24px; }
        .welcome-card {
            background: white;
            border-radius: 16px;
            padding: 48px;
            text-align: center;
            box-shadow: 0 2px 12px rgba(0,0,0,0.06);
            margin-bottom: 32px;
        }
        .welcome-card .big-avatar {
            width: 88px;
            height: 88px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            margin: 0 auto 24px;
            color: white;
            font-size: 40px;
            font-weight: 700;
            box-shadow: 0 4px 15px rgba(102, 126, 234, 0.3);
        }
        .welcome-card h2 { color: #1a1a2e; margin-bottom: 8px; font-size: 26px; }
        .welcome-card .welcome-sub { color: #888; font-size: 15px; margin-bottom: 24px; }
        .status-badge {
            display: inline-flex;
            align-items: center;
            gap: 6px;
            background: #f0fff4;
            color: #38a169;
            padding: 6px 16px;
            border-radius: 20px;
            font-size: 13px;
            font-weight: 500;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            background: #38a169;
            border-radius: 50%;
        }
        .section-title { font-size: 18px; color: #333; margin-bottom: 20px; font-weight: 600; }
        .features {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 20px;
            margin-bottom: 32px;
        }
        .feature-card {
            background: white;
            border-radius: 12px;
            padding: 28px 24px;
            text-align: center;
            box-shadow: 0 2px 8px rgba(0,0,0,0.05);
            transition: transform 0.2s, box-shadow 0.2s;
            cursor: default;
        }
        .feature-card:hover {
            transform: translateY(-3px);
            box-shadow: 0 6px 20px rgba(0,0,0,0.1);
        }
        .feature-card .icon { font-size: 40px; margin-bottom: 14px; display: block; }
        .feature-card h3 { color: #333; font-size: 16px; margin-bottom: 6px; }
        .feature-card p { color: #999; font-size: 13px; line-height: 1.5; }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 32px;
        }
        .info-card {
            background: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.05);
        }
        .info-card .label {
            font-size: 12px;
            color: #999;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            margin-bottom: 6px;
        }
        .info-card .value { font-size: 18px; color: #333; font-weight: 600; }
        .footer-bar {
            text-align: center;
            color: #bbb;
            font-size: 12px;
            padding: 24px;
            border-top: 1px solid #eee;
            margin-top: 32px;
        }
        @media (max-width: 640px) {
            .navbar { padding: 0 16px; }
            .content { padding: 20px 12px; }
            .welcome-card { padding: 32px 20px; }
            .features { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <nav class="navbar">
        <div class="brand"><span>💬</span> MyChat</div>
        <div class="nav-links">
            <a href="/home">🏠 首页</a>
            <a href="#">💬 聊天</a>
            <a href="#">📞 联系人</a>
            <a href="#">⚙️ 设置</a>
        </div>
        <div class="user-menu">
            <span class="username">👤 {{USERNAME}}</span>
            <div class="avatar">{{AVATAR}}</div>
            <a href="/logout" class="logout-btn">退出登录</a>
        </div>
    </nav>
    <div class="content">
        <div class="welcome-card">
            <div class="big-avatar">{{AVATAR}}</div>
            <h2>欢迎回来，{{USERNAME}}！</h2>
            <p class="welcome-sub">你已成功登录 MyChat 聊天室</p>
            <div class="status-badge">
                <span class="status-dot"></span>
                在线
            </div>
        </div>
        <h3 class="section-title">📋 账号信息</h3>
        <div class="info-grid">
            <div class="info-card">
                <div class="label">用户名</div>
                <div class="value">{{USERNAME}}</div>
            </div>
            <div class="info-card">
                <div class="label">状态</div>
                <div class="value" style="color:#38a169;">● 在线</div>
            </div>
            <div class="info-card">
                <div class="label">登录时间</div>
                <div class="value">{{LOGIN_TIME}}</div>
            </div>
            <div class="info-card">
                <div class="label">角色</div>
                <div class="value">成员</div>
            </div>
        </div>
        <h3 class="section-title">🚀 功能</h3>
        <div class="features">
            <div class="feature-card">
                <span class="icon">💬</span>
                <h3>群聊消息</h3>
                <p>实时多人聊天，支持文字和表情</p>
            </div>
            <div class="feature-card">
                <span class="icon">🔒</span>
                <h3>私密聊天</h3>
                <p>端到端加密的一对一通信</p>
            </div>
            <div class="feature-card">
                <span class="icon">📢</span>
                <h3>系统通知</h3>
                <p>实时推送提醒，不错过任何消息</p>
            </div>
            <div class="feature-card">
                <span class="icon">📎</span>
                <h3>文件分享</h3>
                <p>支持图片、文档等文件传输</p>
            </div>
        </div>
    </div>
    <div class="footer-bar">
        MyMuduo HTTP Server Demo · Powered by C++ Reactor Network Framework
    </div>
</body>
</html>
)html";

// ============================================================
// 4. 用户存储（线程安全）
// ============================================================
class UserStore {
public:
    // 注册：成功返回 true，失败返回 false 并设置 error
    bool Register(const std::string& username, const std::string& password,
                  std::string& error) {
        std::lock_guard<std::mutex> lock(_mutex);

        // 验证用户名格式：4-20 位字母、数字、下划线
        if (username.size() < 4 || username.size() > 20) {
            error = "用户名长度需要 4-20 位";
            return false;
        }
        for (char c : username) {
            if (!isalnum(c) && c != '_') {
                error = "用户名只能包含字母、数字和下划线";
                return false;
            }
        }

        // 检查用户名是否已存在
        if (_users.count(username)) {
            error = "该用户名已被注册";
            return false;
        }

        // 验证密码强度
        if (password.size() < 6) {
            error = "密码至少需要 6 位字符";
            return false;
        }

        // 存储（明文，演示用）
        _users[username] = password;
        return true;
    }

    // 登录验证
    bool Authenticate(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _users.find(username);
        if (it == _users.end()) return false;
        return it->second == password;
    }

    // 检查用户是否存在
    bool Exists(const std::string& username) {
        std::lock_guard<std::mutex> lock(_mutex);
        return _users.count(username) > 0;
    }

private:
    std::unordered_map<std::string, std::string> _users;
    std::mutex _mutex;
};

// ============================================================
// 5. Session 管理（线程安全）
// ============================================================
class SessionManager {
public:
    // 创建 session，返回 token
    std::string CreateSession(const std::string& username) {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string token = GenerateToken();
        _sessions[token] = username;
        return token;
    }

    // 验证 session，返回用户名（空串表示无效）
    std::string GetUser(const std::string& token) {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(token);
        if (it == _sessions.end()) return "";
        return it->second;
    }

    // 删除 session
    void DestroySession(const std::string& token) {
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.erase(token);
    }

private:
    static std::string GenerateToken() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static const char* chars = "0123456789abcdef";
        std::string token(32, '\0');
        for (int i = 0; i < 32; ++i)
            token[i] = chars[gen() % 16];
        return token;
    }

    std::unordered_map<std::string, std::string> _sessions;
    std::mutex _mutex;
};

// ============================================================
// 6. 工具函数
// ============================================================

// 替换模板占位符
static std::string Replace(const std::string& tmpl,
                           const std::string& key, const std::string& val) {
    std::string result = tmpl;
    auto pos = result.find(key);
    if (pos != std::string::npos) {
        result.replace(pos, key.size(), val);
    }
    return result;
}

// 替换全部占位符
static std::string ReplaceAll(const std::string& tmpl,
                              const std::unordered_map<std::string, std::string>& vars) {
    std::string result = tmpl;
    for (const auto& [key, val] : vars) {
        size_t pos = 0;
        while ((pos = result.find(key, pos)) != std::string::npos) {
            result.replace(pos, key.size(), val);
            pos += val.size();
        }
    }
    return result;
}

// 从 Cookie 头中提取指定名称的值
static std::string GetCookie(const HttpRequest& req, const std::string& name) {
    auto it = req.headers.find("cookie");
    if (it == req.headers.end()) return "";
    const std::string& cookie = it->second;
    auto pos = cookie.find(name + "=");
    if (pos == std::string::npos) return "";
    pos += name.size() + 1;
    auto end = cookie.find(';', pos);
    if (end == std::string::npos) end = cookie.size();
    return cookie.substr(pos, end - pos);
}

// 解析 x-www-form-urlencoded body
static std::unordered_map<std::string, std::string> ParseFormBody(const std::string& body) {
    std::unordered_map<std::string, std::string> form;
    std::istringstream iss(body);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos)
            form[UrlDecode(pair.substr(0, pos))] = UrlDecode(pair.substr(pos + 1));
        else
            form[UrlDecode(pair)] = "";
    }
    return form;
}

// 获取当前时间字符串
static std::string CurrentTimeStr() {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm_info);
    return std::string(buf);
}

// ============================================================
// 7. 路由处理
// ============================================================
class HttpRouter {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    void Get(const std::string& path, Handler h)  { _get[path] = std::move(h); }
    void Post(const std::string& path, Handler h) { _post[path] = std::move(h); }

    HttpResponse Route(const HttpRequest& req) {
        if (req.method == "GET") {
            auto it = _get.find(req.path);
            if (it != _get.end()) return it->second(req);
        } else if (req.method == "POST") {
            auto it = _post.find(req.path);
            if (it != _post.end()) return it->second(req);
        }
        return HttpResponse::NotFound();
    }

private:
    std::unordered_map<std::string, Handler> _get;
    std::unordered_map<std::string, Handler> _post;
};

// ============================================================
// 8. 主服务
// ============================================================
int main(int argc, char* argv[]) {
    int port = 8888;
    if (argc > 1) port = std::stoi(argv[1]);

    EventLoop loop;

    // 全局状态
    UserStore users;
    SessionManager sessions;

    // — 组装路由 —
    HttpRouter router;

    // ====== GET / 或 /login → 登录页 ======
    auto serveLogin = [&](const HttpRequest& req) -> HttpResponse {
        std::string token = GetCookie(req, "session");
        std::string user = sessions.GetUser(token);
        if (!user.empty()) {
            // 已登录，跳转首页
            return HttpResponse::Redirect("/home");
        }

        // 检查 URL 参数中的错误/消息
        std::string msg_html;
        auto err_it = req.query_params.find("error");
        if (err_it != req.query_params.end()) {
            const std::string& err = err_it->second;
            if (err == "reg_success") {
                msg_html = R"(<div class="success">✅ 注册成功！请使用新账号登录</div>)";
            } else if (err == "wrong") {
                msg_html = R"(<div class="error">❌ 用户名或密码错误</div>)";
            } else if (err == "empty") {
                msg_html = R"(<div class="error">⚠️ 用户名和密码不能为空</div>)";
            } else if (err == "not_logged_in") {
                msg_html = R"(<div class="error">🔒 请先登录后再访问</div>)";
            }
        }

        std::string page = Replace(kLoginPage, "{{MESSAGE}}", msg_html);
        return HttpResponse::Ok(page);
    };

    router.Get("/", serveLogin);
    router.Get("/login", serveLogin);

    // ====== POST /login → 处理登录 ======
    router.Post("/login", [&](const HttpRequest& req) -> HttpResponse {
        auto form = ParseFormBody(req.body);
        std::string username = form["username"];
        std::string password = form["password"];

        if (username.empty() || password.empty()) {
            return HttpResponse::Redirect("/login?error=empty");
        }

        if (!users.Authenticate(username, password)) {
            return HttpResponse::Redirect("/login?error=wrong");
        }

        // 登录成功，创建 session
        std::string token = sessions.CreateSession(username);

        HttpResponse resp = HttpResponse::Redirect("/home");
        resp.set_cookie = "session=" + token + "; Path=/; HttpOnly; Max-Age=3600";
        std::cout << "[LOGIN] ✅ 用户 '" << username
                  << "' 登录成功, token=" << token << std::endl;
        return resp;
    });

    // ====== GET /register → 注册页 ======
    router.Get("/register", [&](const HttpRequest& req) -> HttpResponse {
        std::string token = GetCookie(req, "session");
        std::string user = sessions.GetUser(token);
        if (!user.empty()) {
            return HttpResponse::Redirect("/home");
        }

        std::string msg_html;
        auto err_it = req.query_params.find("error");
        if (err_it != req.query_params.end()) {
            const std::string& err = err_it->second;
            if (err == "empty") {
                msg_html = R"(<div class="error">⚠️ 请填写所有必填字段</div>)";
            } else if (err == "password_mismatch") {
                msg_html = R"(<div class="error">❌ 两次输入的密码不一致</div>)";
            } else if (err == "username_taken") {
                msg_html = R"(<div class="error">❌ 该用户名已被注册</div>)";
            } else if (err == "weak_password") {
                msg_html = R"(<div class="error">❌ 密码强度不足，至少需要 6 位字符</div>)";
            } else if (err == "invalid_username") {
                msg_html = R"(<div class="error">❌ 用户名格式不正确（4-20 位字母、数字或下划线）</div>)";
            }
        }

        std::string page = Replace(kRegisterPage, "{{MESSAGE}}", msg_html);
        return HttpResponse::Ok(page);
    });

    // ====== POST /register → 处理注册 ======
    router.Post("/register", [&](const HttpRequest& req) -> HttpResponse {
        auto form = ParseFormBody(req.body);
        std::string username = form["username"];
        std::string password = form["password"];
        std::string confirm = form["confirm_password"];

        // 空值检查
        if (username.empty() || password.empty() || confirm.empty()) {
            return HttpResponse::Redirect("/register?error=empty");
        }

        // 密码一致性检查
        if (password != confirm) {
            return HttpResponse::Redirect("/register?error=password_mismatch");
        }

        // 注册
        std::string error;
        if (!users.Register(username, password, error)) {
            if (error.find("已被注册") != std::string::npos)
                return HttpResponse::Redirect("/register?error=username_taken");
            else if (error.find("密码") != std::string::npos)
                return HttpResponse::Redirect("/register?error=weak_password");
            else
                return HttpResponse::Redirect("/register?error=invalid_username");
        }

        std::cout << "[REGISTER] ✅ 新用户 '" << username << "' 注册成功" << std::endl;
        return HttpResponse::Redirect("/login?error=reg_success");
    });

    // ====== GET /home → 首页（需登录） ======
    router.Get("/home", [&](const HttpRequest& req) -> HttpResponse {
        std::string token = GetCookie(req, "session");
        std::string username = sessions.GetUser(token);
        if (username.empty()) {
            return HttpResponse::Redirect("/login?error=not_logged_in");
        }

        std::string avatar(1, static_cast<char>(std::toupper(username[0])));
        std::string html = kHomePage;
        html = Replace(html, "{{USERNAME}}", username);
        html = Replace(html, "{{AVATAR}}", avatar);
        html = Replace(html, "{{LOGIN_TIME}}", CurrentTimeStr());
        return HttpResponse::Ok(html);
    });

    // ====== GET /logout → 退出登录 ======
    router.Get("/logout", [&](const HttpRequest& req) -> HttpResponse {
        std::string token = GetCookie(req, "session");
        if (!token.empty()) {
            std::string user = sessions.GetUser(token);
            if (!user.empty()) {
                std::cout << "[LOGOUT] 用户 '" << user << "' 退出登录" << std::endl;
            }
            sessions.DestroySession(token);
        }
        HttpResponse resp = HttpResponse::Redirect("/login");
        resp.set_cookie = "session=; Path=/; Max-Age=0";
        return resp;
    });

    // — 创建 TcpServer —
    // timeout = 120s，4 个 worker 线程
    TcpServer server(&loop, port, 4, 120);

    server.SetMessageCallback(
        [&](TcpConnection::Ptr conn, Buffer* buf) {
            std::string raw = buf->RetrieveAllAsString();
            if (raw.empty()) return;

            HttpRequest req;
            if (!ParseHttpRequest(raw, req)) {
                HttpResponse resp;
                resp.status_code = 400;
                resp.status_msg = "Bad Request";
                resp.body = "<html><head><meta charset=\"UTF-8\"></head><body><h1>400 Bad Request</h1></body></html>";
                conn->Send(resp.ToString());
                return;
            }

            std::cout << "[" << req.method << "] " << req.path
                      << " (body=" << req.body.size() << " bytes)" << std::endl;

            HttpResponse resp = router.Route(req);
            conn->Send(resp.ToString());
        });

    server.SetConnectCallback(
        [](TcpConnection::Ptr conn) {
            std::cout << "[CONNECT] fd=" << conn->Fd() << std::endl;
        });

    server.Start();

    std::cout << "\n==========================================\n";
    std::cout << "  🚀 MyChat HTTP Server 已启动\n";
    std::cout << "  地址: http://localhost:" << port << "/\n";
    std::cout << "  线程: 1 主线程 + 4 worker 线程\n";
    std::cout << "  页面: 登录页 / 注册页 / 首页\n";
    std::cout << "  按 Ctrl+C 停止\n";
    std::cout << "==========================================\n\n";

    loop.Loop();
    return 0;
}

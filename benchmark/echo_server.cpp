/**
 * HTTP Echo 压测服务端
 *
 * 基于 MyMuduo 网络库 + cpp-httplib，用于 wrk 压测。
 *
 * 用法：
 *   1. 编译：在 CMakeLists.txt 中添加本文件
 *   2. 运行：./build/echo_bench
 *   3. 压测：wrk -t4 -c1000 -d30s http://127.0.0.1:8080/echo
 *   4. 长连接：wrk -t4 -c1000 -d30s -H "Connection: Keep-Alive" http://127.0.0.1:8080/echo
 *
 * 压测指标：
 *   - QPS (Requests/sec)
 *   - 平均延迟 (Latency Avg)
 *   - P99 延迟 (Latency 99%)
 *   - 并发连接数 (Connections)
 */

#include "net/TcpServer.h"
#include "net/EventLoop.h"
#include "net/Log.h"

#include <iostream>
#include <string>
#include <sstream>
#include <thread>

// 简易 HTTP 响应构建
static std::string MakeHttpResponse(const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: Keep-Alive\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

// 简易 HTTP 请求解析（仅用于 echo 测试）
static bool ParseHttpRequest(Buffer* buf, std::string& out_path) {
    std::string data(buf->Peek(), buf->ReadableSize());
    // 找第一行：GET /path HTTP/1.1
    size_t pos = data.find("\r\n");
    if (pos == std::string::npos) return false;

    std::string line = data.substr(0, pos);
    // 找 header 结束标记
    size_t header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;

    // 解析路径
    size_t start = line.find(' ');
    if (start == std::string::npos) return false;
    size_t end = line.find(' ', start + 1);
    if (end == std::string::npos) return false;
    out_path = line.substr(start + 1, end - start - 1);

    // 消费整个 HTTP 请求
    buf->Retrieve(header_end + 4);
    return true;
}

int main(int argc, char* argv[]) {
    uint16_t port = 8080;
    size_t thread_num = 4;

    if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc > 2) thread_num = static_cast<size_t>(std::stoi(argv[2]));

    std::cout << "========================================" << std::endl;
    std::cout << "  MyMuduo HTTP Echo Benchmark Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Listen Port: " << port << std::endl;
    std::cout << "  Worker Threads: " << thread_num << std::endl;
    std::cout << std::endl;
    std::cout << "  wrk 压测命令:" << std::endl;
    std::cout << "  wrk -t4 -c1000 -d30s http://127.0.0.1:" << port << "/echo" << std::endl;
    std::cout << "  wrk -t4 -c5000 -d30s http://127.0.0.1:" << port << "/echo" << std::endl;
    std::cout << "========================================" << std::endl;

    EventLoop base_loop;
    TcpServer server(&base_loop, port, thread_num, 60);

    // 设置消息回调：echo 模式
    server.SetMessageCallback(
        [](const TcpConnection::Ptr& conn, Buffer* buf) {
            std::string path;
            if (ParseHttpRequest(buf, path)) {
                std::string body = R"({"status":"ok","path":")" + path + R"(","msg":"hello from MyMuduo"})";
                conn->Send(MakeHttpResponse(body));
            }
        });

    // 设置连接回调
    server.SetConnectCallback(
        [](const TcpConnection::Ptr& conn) {
            if (conn->Connected()) {
                // LOG_INFO("New connection: fd=%d", conn->Fd());
            }
        });

    server.Start();
    std::cout << "  Server started! Ready for benchmark." << std::endl;
    std::cout << "  Press Ctrl+C to stop." << std::endl;

    base_loop.Loop();
    return 0;
}

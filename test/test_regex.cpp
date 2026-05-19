#include <iostream>
#include <string>
#include <regex>

int main() {
    // HTTP 请求正则
    std::regex httpRegex(
        R"(^(GET|POST|PUT|DELETE|HEAD|OPTIONS|PATCH)\s+([^\s]+)\s+HTTP/1\.[01]\r?\nHost:\s+([^\r\n]+))",
        std::regex::icase  // 忽略大小写（HTTP 方法/Header 不区分大小写）
    );

    // 测试用例
    std::string testRequest =
        "GET /index.html HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "User-Agent: curl/8.0\r\n";

    std::smatch match;

    if (std::regex_search(testRequest, match, httpRegex)) {
        std::cout << "匹配成功\n";
        std::cout << "HTTP 方法: " << match[1] << "\n";
        std::cout << "请求路径: " << match[2] << "\n";
        std::cout << "Host: " << match[3] << "\n";
    } else {
        std::cout << "未匹配\n";
    }

    return 0;
}
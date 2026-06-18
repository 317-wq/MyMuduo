#pragma once

#include <stdexcept>
#include <string>
#include <cstring>

namespace mymuduo {

// 框架层异常：用于替代 perror / cerr 调试输出
class Exception : public std::runtime_error {
public:
    explicit Exception(const std::string& msg)
        : std::runtime_error(msg) {}

    /// 携带 errno 信息的异常
    static Exception FromErrno(const std::string& prefix) {
        return Exception(prefix + ": " + std::strerror(errno));
    }
};

}

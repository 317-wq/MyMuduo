/*
    服务端启动边界回归测试（B09）

    覆盖：
    - TimeWheel 拒绝 timeout=0，避免取模崩溃
    - Acceptor 在端口被占用时抛出启动错误
    - TcpServer 把 Acceptor 的启动错误向上传递
*/

#include "net/Acceptor.h"
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/TimeWheel.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <stdexcept>

namespace {

// 先占用一个内核分配的端口，用于验证 bind 失败路径。
class ReservedPort {
public:
    ReservedPort() {
        _fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_fd < 0) return;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = 0;
        if (::bind(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(_fd);
            _fd = -1;
            return;
        }
        if (::listen(_fd, 1) < 0) {
            ::close(_fd);
            _fd = -1;
            return;
        }

        socklen_t len = sizeof(addr);
        if (::getsockname(_fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            ::close(_fd);
            _fd = -1;
            return;
        }
        _port = ntohs(addr.sin_port);
    }

    ~ReservedPort() {
        if (_fd >= 0) ::close(_fd);
    }

    int fd() const { return _fd; }
    uint16_t port() const { return _port; }

private:
    int _fd = -1;
    uint16_t _port = 0;
};

}  // namespace

// timeout=0 必须在构造阶段被拒绝。
TEST(ServerBoundaryTest, TimeWheelRejectsZeroTimeout) {
    EventLoop loop;
    EXPECT_THROW({ TimeWheel wheel(&loop, 0); }, std::invalid_argument);
}

// 端口被占用时 Acceptor 必须抛错，而不是静默启动。
TEST(ServerBoundaryTest, AcceptorRejectsOccupiedPort) {
    ReservedPort reserved;
    ASSERT_GE(reserved.fd(), 0);
    EventLoop loop;
    EXPECT_THROW({ Acceptor acceptor(&loop, reserved.port()); }, std::runtime_error);
}

// TcpServer 必须把 Acceptor 的启动错误向上传递。
TEST(ServerBoundaryTest, TcpServerRejectsOccupiedPort) {
    ReservedPort reserved;
    ASSERT_GE(reserved.fd(), 0);
    EventLoop loop;
    EXPECT_THROW({ TcpServer server(&loop, reserved.port(), 0, 60); }, std::runtime_error);
}

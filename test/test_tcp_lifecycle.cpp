/*
    TCP 连接生命周期回归测试（B01-B04）

    覆盖：
    - B01：连接关闭后 TcpServer 立即回收连接，不等时间轮超时
    - B02：ET 模式下并发建连全部被 accept
    - B03：ET 模式下部分写后继续发送，直到 out_buffer 排空
    - B04：客户端 send 后立即半关闭，最后一批数据仍分发给业务层

    测试使用单线程 TcpServer（thread_num = 0），让连接直接挂在 base_loop 上，
    便于稳定观察连接数与读写行为。
*/

#include "net/EventLoop.h"
#include "net/TcpServer.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

// 大响应测试数据：超过默认 socket 发送缓冲区，确保出现部分写。
constexpr int kLargePayload = 8 * 1024 * 1024;
// 半关闭测试数据：一次读循环内可读完，便于触发“读完数据后立刻读到 EOF”的分支。
constexpr int kHalfClosePayload = 64 * 1024;

// 连接到指定端口；recv_buffer > 0 时在 connect 前设置接收缓冲区。
int ConnectTo(uint16_t port, int recv_buffer = 0) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (recv_buffer > 0) {
        ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_buffer, sizeof(recv_buffer));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// 轮询等待条件成立，超时返回 false。
template <typename Pred>
bool WaitFor(Pred pred, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

}  // namespace

// TCP 生命周期测试夹具：启动一个小型 TcpServer，并让 base_loop 在独立线程运行。
class TcpLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        _loop = std::make_unique<EventLoop>();
        // thread_num = 0：连接直接由 base_loop 处理，便于测试观察。
        _server = std::make_unique<TcpServer>(_loop.get(), 0, 0, 60);

        _server->SetMessageCallback(
            [this](const TcpConnection::Ptr&, Buffer* buf) {
                std::lock_guard<std::mutex> lock(_msg_mutex);
                _received += buf->RetrieveAllAsString();
                _msg_cv.notify_all();
            });

        _server->SetConnectCallback(
            [this](const TcpConnection::Ptr& conn) {
                if (_send_on_connect.load(std::memory_order_relaxed)) {
                    conn->Send(std::string(kLargePayload, 'x'));
                }
            });

        _server->Start();
        _port = _server->Port();
        ASSERT_NE(_port, 0);

        _loop_thread = std::thread([this] {
            _loop_started.store(true, std::memory_order_release);
            _loop->Loop();
        });
        // 等 Loop 真正进入运行态，避免 TearDown 的 Quit 被 Loop 内的 _running = true 覆盖。
        while (!_loop_started.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        if (_loop) {
            _loop->Quit();
        }
        if (_loop_thread.joinable()) {
            _loop_thread.join();
        }
        _server.reset();
        _loop.reset();
    }

    std::unique_ptr<EventLoop> _loop;
    std::unique_ptr<TcpServer> _server;
    std::thread _loop_thread;
    std::atomic<bool> _loop_started{false};
    std::atomic<bool> _send_on_connect{false};
    uint16_t _port = 0;

    std::mutex _msg_mutex;
    std::condition_variable _msg_cv;
    std::string _received;
};

// B01：连接关闭后应立即从 TcpServer 的 connections 中移除。
TEST_F(TcpLifecycleTest, ConnectThenCloseReleasesConnection) {
    int fd = ConnectTo(_port);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(WaitFor([this] { return _server->ConnectionCount() == 1; }, 2000));

    ::close(fd);
    EXPECT_TRUE(WaitFor([this] { return _server->ConnectionCount() == 0; }, 2000));
}

// B02：并发建连时，accept 循环应把监听队列一次排空。
TEST_F(TcpLifecycleTest, BurstConnectionsAreAllAccepted) {
    constexpr int kClients = 100;
    std::vector<int> fds(kClients, -1);
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> clients;
    clients.reserve(kClients);

    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([this, &fds, &ready, &go, i] {
            ready.fetch_add(1, std::memory_order_relaxed);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            fds[i] = ConnectTo(_port);
        });
    }

    while (ready.load(std::memory_order_relaxed) < kClients) {
        std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);
    for (auto& client : clients) {
        client.join();
    }

    int connected = 0;
    for (int fd : fds) {
        if (fd >= 0) ++connected;
    }
    EXPECT_EQ(connected, kClients);
    EXPECT_TRUE(WaitFor([this] {
        return _server->ConnectionCount() == static_cast<size_t>(kClients);
    }, 5000));

    for (int fd : fds) {
        if (fd >= 0) ::close(fd);
    }
    EXPECT_TRUE(WaitFor([this] { return _server->ConnectionCount() == 0; }, 5000));
}

// B03：大响应遇到慢客户端时，HandleWrite 应循环发送，直到 out_buffer 排空。
TEST_F(TcpLifecycleTest, PartialWriteDrainsOutBuffer) {
    _send_on_connect.store(true, std::memory_order_relaxed);
    int fd = ConnectTo(_port, 4096);
    ASSERT_GE(fd, 0);

    // 先不读，让服务端把发送缓冲区写满，制造部分写。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const int flags = ::fcntl(fd, F_GETFL, 0);
    ASSERT_GE(flags, 0);
    ASSERT_GE(::fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);

    std::string data;
    data.reserve(kLargePayload);
    char buffer[64 * 1024];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (data.size() < static_cast<size_t>(kLargePayload)
           && std::chrono::steady_clock::now() < deadline) {
        pollfd pfd{fd, POLLIN, 0};
        int ret = ::poll(&pfd, 1, 100);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            data.append(buffer, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }

    EXPECT_EQ(data.size(), static_cast<size_t>(kLargePayload));
    EXPECT_EQ(data, std::string(kLargePayload, 'x'));

    ::close(fd);
    EXPECT_TRUE(WaitFor([this] { return _server->ConnectionCount() == 0; }, 2000));
}

// B04：客户端 send 后立即 shutdown(SHUT_WR)，已读数据必须先交给业务层再关闭。
TEST_F(TcpLifecycleTest, SendThenHalfCloseDeliversBufferedData) {
    const std::string payload(kHalfClosePayload, 'y');
    int fd = ConnectTo(_port);
    ASSERT_GE(fd, 0);

    size_t sent = 0;
    while (sent < payload.size()) {
        ssize_t n = ::send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
        ASSERT_GT(n, 0);
        sent += static_cast<size_t>(n);
    }
    ASSERT_EQ(::shutdown(fd, SHUT_WR), 0);

    {
        std::unique_lock<std::mutex> lock(_msg_mutex);
        EXPECT_TRUE(_msg_cv.wait_for(lock, std::chrono::seconds(2), [this] {
            return _received.size() >= static_cast<size_t>(kHalfClosePayload);
        }));
        EXPECT_EQ(_received, payload);
    }

    ::close(fd);
    EXPECT_TRUE(WaitFor([this] { return _server->ConnectionCount() == 0; }, 2000));
}

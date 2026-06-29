/**
 * TCP 并发连接 & 吞吐量压测工具
 *
 * 测试目标：
 *   1. 最大并发连接数
 *   2. 连接建立速率 (conn/sec)
 *   3. 消息吞吐量 (msg/sec)
 *   4. 消息延迟分布
 *
 * 用法：
 *   1. 先启动 echo_server：  ./build/echo_bench 8080 4
 *   2. 运行压测客户端：      ./build/tcp_bench 127.0.0.1 8080 5000 100 30
 *                           ./build/tcp_bench <host> <port> <conn_num> <msg_per_conn> <duration_sec>
 *
 * 输出：
 *   - 成功建立的连接数
 *   - 连接建立耗时
 *   - 总发送/接收消息数
 *   - 消息延迟 (avg / P50 / P99 / P999 / max)
 *   - 吞吐量 (msg/sec)
 */

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

// ============ 简易命令行参数解析 ============
struct Config {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    int conn_num = 1000;         // 目标并发连接数
    int msgs_per_conn = 100;     // 每连接发送消息数
    int duration_sec = 30;       // 压测持续时间（0 = 不限时，发完为止）
};

// ============ 全局统计 ============
struct Stats {
    std::atomic<int64_t> connect_success{0};
    std::atomic<int64_t> connect_fail{0};
    std::atomic<int64_t> send_total{0};
    std::atomic<int64_t> recv_total{0};
    std::atomic<int64_t> send_bytes{0};
    std::atomic<int64_t> recv_bytes{0};
    std::atomic<bool> stop{false};

    // 延迟记录（微秒），每连接取中位数后汇总
    std::mutex latency_mutex;
    std::vector<int64_t> latencies_us;  // 所有消息延迟

    void recordLatency(int64_t us) {
        std::lock_guard<std::mutex> lock(latency_mutex);
        latencies_us.push_back(us);
    }
};

// ============ 设置非阻塞 ============
static bool SetNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

// ============ 创建 TCP 连接 ============
static int CreateConnection(const std::string& host, uint16_t port, int timeout_ms = 5000) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    SetNonBlock(fd);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    // 用 select 等待连接完成
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    ret = select(fd + 1, nullptr, &wfds, nullptr, &tv);
    if (ret <= 0) {
        close(fd);
        return -1;
    }

    // 检查连接是否成功
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
    if (err != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// ============ 消息协议（与 MyMuduo 协议兼容） ============
static std::string BuildEchoMessage() {
    // 简易协议：4字节长度 + 2字节类型 + JSON body
    std::string body = R"({"type":"echo","data":"hello benchmark!","timestamp":1234567890})";
    uint32_t body_len = htonl(static_cast<uint32_t>(body.size()));
    uint16_t msg_type = htons(1);  // 类型 1

    std::string frame;
    frame.append(reinterpret_cast<const char*>(&body_len), 4);
    frame.append(reinterpret_cast<const char*>(&msg_type), 2);
    frame.append(body);
    return frame;
}

// ============ 单连接压测工作线程 ============
static void WorkerThread(const Config& cfg, Stats& stats, int thread_id, int conns_per_thread) {
    struct MessageRecord {
        int64_t send_time_us;
        int fd;
    };

    std::vector<int> fds;
    fds.reserve(conns_per_thread);

    // ===== 阶段 1：建立连接 =====
    for (int i = 0; i < conns_per_thread && !stats.stop; ++i) {
        int fd = CreateConnection(cfg.host, cfg.port, 5000);
        if (fd >= 0) {
            fds.push_back(fd);
            stats.connect_success++;
        } else {
            stats.connect_fail++;
        }
        // 限速建立连接（避免瞬间打爆 listen backlog）
        if (i % 100 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (fds.empty()) return;

    // ===== 阶段 2：发送消息 + 接收响应 =====
    std::string msg = BuildEchoMessage();
    char recv_buf[65536];

    for (int msg_idx = 0; msg_idx < cfg.msgs_per_conn && !stats.stop; ++msg_idx) {
        for (int fd : fds) {
            if (stats.stop) break;

            // 发送
            auto t1 = std::chrono::steady_clock::now();
            ssize_t n = send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
            if (n <= 0) {
                stats.stop = true;
                break;
            }
            stats.send_total++;
            stats.send_bytes += msg.size();

            // 接收（简易：一次 recv，读足够字节）
            // 协议 frame = 6 header + JSON body
            size_t total_recv = 0;
            size_t expected = msg.size();
            while (total_recv < expected && !stats.stop) {
                n = recv(fd, recv_buf + total_recv, sizeof(recv_buf) - total_recv, 0);
                if (n <= 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                        continue;
                    }
                    stats.stop = true;
                    break;
                }
                total_recv += n;
            }
            auto t2 = std::chrono::steady_clock::now();

            if (total_recv >= expected) {
                stats.recv_total++;
                stats.recv_bytes += total_recv;

                int64_t latency_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
                stats.recordLatency(latency_us);
            }
        }
    }

    // ===== 清理 =====
    for (int fd : fds) {
        close(fd);
    }
}

// ============ 输出统计 ============
static void PrintStats(const Stats& stats, double elapsed_sec) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  TCP Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Duration:             " << elapsed_sec << " sec" << std::endl;
    std::cout << "  Connections Success:  " << stats.connect_success.load() << std::endl;
    std::cout << "  Connections Failed:   " << stats.connect_fail.load() << std::endl;
    std::cout << "  Messages Sent:        " << stats.send_total.load() << std::endl;
    std::cout << "  Messages Recv:        " << stats.recv_total.load() << std::endl;
    std::cout << "  Throughput:           "
              << static_cast<int64_t>(stats.recv_total.load() / elapsed_sec) << " msg/sec" << std::endl;

    double send_mbps = stats.send_bytes.load() / elapsed_sec / 1024 / 1024;
    double recv_mbps = stats.recv_bytes.load() / elapsed_sec / 1024 / 1024;
    std::cout << "  Send BW:              " << send_mbps << " MB/sec" << std::endl;
    std::cout << "  Recv BW:              " << recv_mbps << " MB/sec" << std::endl;

    // 延迟统计
    std::vector<int64_t> latencies;
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stats.latency_mutex));
        latencies = stats.latencies_us;
    }

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());

        int64_t sum = 0;
        for (auto l : latencies) sum += l;
        double avg_us = static_cast<double>(sum) / latencies.size();

        size_t n = latencies.size();
        int64_t p50 = latencies[n * 50 / 100];
        int64_t p90 = latencies[n * 90 / 100];
        int64_t p99 = latencies[n * 99 / 100];
        int64_t p999 = latencies[n * 999 / 1000];
        int64_t pmax = latencies.back();
        int64_t pmin = latencies.front();

        std::cout << "----------------------------------------" << std::endl;
        std::cout << "  Latency (microseconds):" << std::endl;
        std::cout << "    Samples:  " << latencies.size() << std::endl;
        std::cout << "    Avg:      " << static_cast<int64_t>(avg_us) << " us ("
                  << avg_us / 1000.0 << " ms)" << std::endl;
        std::cout << "    Min:      " << pmin << " us" << std::endl;
        std::cout << "    P50:      " << p50 << " us (" << p50 / 1000.0 << " ms)" << std::endl;
        std::cout << "    P90:      " << p90 << " us (" << p90 / 1000.0 << " ms)" << std::endl;
        std::cout << "    P99:      " << p99 << " us (" << p99 / 1000.0 << " ms)" << std::endl;
        std::cout << "    P999:     " << p999 << " us (" << p999 / 1000.0 << " ms)" << std::endl;
        std::cout << "    Max:      " << pmax << " us (" << pmax / 1000.0 << " ms)" << std::endl;
    }

    std::cout << "========================================" << std::endl;
}

// ============ main ============
int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE

    Config cfg;
    if (argc > 1) cfg.host = argv[1];
    if (argc > 2) cfg.port = static_cast<uint16_t>(std::stoi(argv[2]));
    if (argc > 3) cfg.conn_num = std::stoi(argv[3]);
    if (argc > 4) cfg.msgs_per_conn = std::stoi(argv[4]);
    if (argc > 5) cfg.duration_sec = std::stoi(argv[5]);

    std::cout << "========================================" << std::endl;
    std::cout << "  MyMuduo TCP Benchmark Client" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Target:       " << cfg.host << ":" << cfg.port << std::endl;
    std::cout << "  Connections:  " << cfg.conn_num << std::endl;
    std::cout << "  Msg/Conn:     " << cfg.msgs_per_conn << std::endl;
    std::cout << "  Duration:     " << cfg.duration_sec << " sec" << std::endl;
    std::cout << "========================================" << std::endl;

    Stats stats;

    // 启动压测线程
    int num_threads = std::min(cfg.conn_num, 64);
    int conns_per_thread = cfg.conn_num / num_threads;
    int remainder = cfg.conn_num % num_threads;

    std::vector<std::thread> threads;
    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        int n = conns_per_thread + (i < remainder ? 1 : 0);
        if (n > 0) {
            threads.emplace_back(WorkerThread, std::ref(cfg), std::ref(stats), i, n);
        }
    }

    // 等待完成或超时
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         end_time - start_time).count() / 1000.0;

    PrintStats(stats, elapsed);
    return 0;
}

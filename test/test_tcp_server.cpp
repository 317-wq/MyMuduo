#include "../include/Channel.h"
#include "../include/EpollPoller.h"
#include "../include/EventLoop.h"
#include "../include/Socket.h"
#include "../include/InetAddress.h"

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <thread>

void PrintDivider() {
    std::cout << "\n========================================\n\n";
}

void TestServerClientEcho() {
    std::cout << "[测试] TCP服务端-客户端 Echo 测试\n";
    PrintDivider();

    // ===== 服务端设置 =====
    EventLoop loop;
    Socket server_socket;
    server_socket.SetReuseAddr();
    server_socket.SetReusePort();
    
    InetAddress server_addr(9999);
    server_socket.Bind(server_addr);
    server_socket.Listen();

    int listen_fd = server_socket.Fd();
    Channel listen_channel(listen_fd);
    loop.AddChannel(&listen_channel);

    bool accept_cb_called = false;
    bool client_read_cb_called = false;
    std::string received_data;

    // 监听socket的读事件回调（接受新连接）
    listen_channel.SetReadCallback([&]() {
        std::cout << "[Server] 新连接到来...\n";
        accept_cb_called = true;

        InetAddress client_addr;
        int client_fd = server_socket.Accept(&client_addr);
        
        if (client_fd > 0) {
            std::cout << "[Server] 接受客户端连接: " << client_addr.Ip() 
                      << ":" << client_addr.Port() << "\n";

            // 创建客户端channel
            auto client_channel = new Channel(client_fd);
            loop.AddChannel(client_channel);

            // 客户端数据读取回调
            client_channel->SetReadCallback([&loop, client_channel, &client_read_cb_called, &received_data]() {
                char buf[1024] = {0};
                int n = read(client_channel->Fd(), buf, sizeof(buf)-1);
                
                if (n > 0) {
                    received_data = buf;
                    std::cout << "[Server] 收到数据: " << buf << "\n";
                    client_read_cb_called = true;

                    // Echo回显
                    write(client_channel->Fd(), buf, n);
                    std::cout << "[Server] 已回显数据\n";
                } else {
                    std::cout << "[Server] 客户端断开连接\n";
                    loop.GetPoller()->RemoveChannel(client_channel);
                    delete client_channel;
                }
            });

            client_channel->EnableRead();
        }
    });

    listen_channel.EnableRead();
    std::cout << "[Server] 服务端已启动，监听端口 9999...\n";

    // ===== 客户端设置（在新线程中运行）=====
    std::thread client_thread([&]() {
        // 等待服务端就绪
        sleep(1);

        std::cout << "[Client] 客户端启动...\n";
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(9999);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("[Client] connect failed");
            return;
        }

        std::cout << "[Client] 连接成功，发送数据...\n";
        const char* msg = "Hello, MyMuduo!";
        write(sockfd, msg, strlen(msg));

        char buf[1024] = {0};
        int n = read(sockfd, buf, sizeof(buf)-1);
        if (n > 0) {
            std::cout << "[Client] 收到回显: " << buf << "\n";
        }

        close(sockfd);
        std::cout << "[Client] 客户端关闭\n";
    });

    // 服务端处理一次事件
    EventLoop::ChannelList active_channels;
    loop.GetPoller()->Poll(&active_channels, 3000);
    
    for (auto ch : active_channels) {
        ch->HandleEvent();
    }

    // 处理客户端数据
    active_channels.clear();
    loop.GetPoller()->Poll(&active_channels, 2000);
    
    for (auto ch : active_channels) {
        ch->HandleEvent();
    }

    client_thread.join();

    std::cout << "\n[测试结果]\n";
    std::cout << "accept_cb_called: " << (accept_cb_called ? "true" : "false") << "\n";
    std::cout << "client_read_cb_called: " << (client_read_cb_called ? "true" : "false") << "\n";
    std::cout << "received_data: " << received_data << "\n";

    bool test_passed = accept_cb_called && client_read_cb_called && 
                       (received_data == "Hello, MyMuduo!");
    std::cout << "\n[Test] " << (test_passed ? "PASSED" : "FAILED") << "\n";
}

void TestMultiClient() {
    std::cout << "\n[测试] 多客户端连接测试\n";
    PrintDivider();

    EventLoop loop;
    Socket server_socket;
    server_socket.SetReuseAddr();
    server_socket.SetReusePort();
    
    InetAddress server_addr(9998);
    server_socket.Bind(server_addr);
    server_socket.Listen();

    int listen_fd = server_socket.Fd();
    Channel listen_channel(listen_fd);
    loop.AddChannel(&listen_channel);

    int client_count = 0;
    const int expected_clients = 3;

    listen_channel.SetReadCallback([&]() {
        InetAddress client_addr;
        int client_fd = server_socket.Accept(&client_addr);
        
        if (client_fd > 0) {
            client_count++;
            std::cout << "[Server] 客户端" << client_count << "连接: " 
                      << client_addr.Ip() << ":" << client_addr.Port() << "\n";

            auto client_channel = new Channel(client_fd);
            loop.AddChannel(client_channel);
            client_channel->EnableRead();
        }
    });

    listen_channel.EnableRead();
    std::cout << "[Server] 服务端启动，监听端口 9998...\n";

    // 创建多个客户端
    std::vector<std::thread> clients;
    for (int i = 0; i < expected_clients; i++) {
        clients.emplace_back([i]() {
            sleep(i * 0.5);
            
            int sockfd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(9998);
            inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
            
            if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) >= 0) {
                std::cout << "[Client" << (i+1) << "] 连接成功\n";
            }
        });
    }

    // 处理所有连接
    for (int i = 0; i < expected_clients; i++) {
        EventLoop::ChannelList active_channels;
        loop.GetPoller()->Poll(&active_channels, 2000);
        
        for (auto ch : active_channels) {
            ch->HandleEvent();
        }
    }

    for (auto& t : clients) {
        t.join();
    }

    std::cout << "\n[测试结果]\n";
    std::cout << "期望客户端数: " << expected_clients << "\n";
    std::cout << "实际连接数: " << client_count << "\n";
    
    bool test_passed = (client_count == expected_clients);
    std::cout << "\n[Test] " << (test_passed ? "PASSED" : "FAILED") << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "      Channel + EpollPoller + EventLoop \n";
    std::cout << "           TCP服务端-客户端测试           \n";
    std::cout << "========================================\n";

    TestServerClientEcho();

    TestMultiClient();

    std::cout << "\n========================================\n";
    std::cout << "            All Tests Completed         \n";
    std::cout << "========================================\n";

    return 0;
}
#include "../include/Channel.h"
#include "../include/EpollPoller.h"
#include "../include/EventLoop.h"
#include "../include/Acceptor.h"
#include "../include/Connector.h"
#include "../include/InetAddress.h"

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <thread>

void PrintDivider() {
    std::cout << "\n========================================\n\n";
}

void TestAcceptorConnectorEcho() {
    std::cout << "[测试] 使用 Acceptor + Connector 实现 Echo 测试\n";
    PrintDivider();

    // ===== 服务端设置 =====
    EventLoop loop;
    
    // 使用 Acceptor 封装监听socket
    Acceptor acceptor(9999, true);
    
    // 获取监听fd并创建channel
    int listen_fd = acceptor.Fd();
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
        int client_fd = acceptor.Accept(&client_addr);
        
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
                    loop.RemoveChannel(client_channel);
                    delete client_channel;
                }
            });

            client_channel->EnableRead();
        }
    });

    listen_channel.EnableRead();
    std::cout << "[Server] 服务端已启动，监听端口 9999...\n";

    // ===== 客户端设置（使用 Connector）=====
    std::thread client_thread([&]() {
        // 等待服务端就绪
        sleep(1);

        std::cout << "[Client] 客户端启动...\n";
        Connector connector(true);
        
        // 使用 Connector 发起连接
        if (connector.Connect("127.0.0.1", 9999)) {
            std::cout << "[Client] 连接成功，发送数据...\n";
            
            int sockfd = connector.Fd();
            const char* msg = "Hello, MyMuduo!";
            write(sockfd, msg, strlen(msg));

            char buf[1024] = {0};
            int n = read(sockfd, buf, sizeof(buf)-1);
            if (n > 0) {
                std::cout << "[Client] 收到回显: " << buf << "\n";
            }

            close(sockfd);
        } else {
            std::cout << "[Client] 连接失败\n";
        }
        
        std::cout << "[Client] 客户端关闭\n";
    });

    // 服务端处理一次事件
    EventLoop::ChannelList active_channels;
    loop.Poll(&active_channels, 3000);
    
    for (auto ch : active_channels) {
        ch->HandleEvent();
    }

    // 处理客户端数据
    active_channels.clear();
    loop.Poll(&active_channels, 2000);
    
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

void TestMultiClientWithAcceptor() {
    std::cout << "\n[测试] 使用 Acceptor 处理多客户端连接\n";
    PrintDivider();

    EventLoop loop;
    Acceptor acceptor(9998, true);
    
    int listen_fd = acceptor.Fd();
    Channel listen_channel(listen_fd);
    loop.AddChannel(&listen_channel);

    int client_count = 0;
    const int expected_clients = 3;

    listen_channel.SetReadCallback([&]() {
        // 循环 accept 所有等待的连接
        while (true) {
            InetAddress client_addr;
            int client_fd = acceptor.Accept(&client_addr);
            
            if (client_fd > 0) {
                client_count++;
                std::cout << "[Server] 客户端" << client_count << "连接: " 
                          << client_addr.Ip() << ":" << client_addr.Port() << "\n";

                auto client_channel = new Channel(client_fd);
                loop.AddChannel(client_channel);
                client_channel->EnableRead();
            } else {
                // 没有更多连接了
                break;
            }
        }
    });

    listen_channel.EnableRead();
    std::cout << "[Server] 服务端启动，监听端口 9998...\n";

    // 创建多个客户端（使用 Connector）
    std::vector<std::thread> clients;
    for (int i = 0; i < expected_clients; i++) {
        clients.emplace_back([i]() {
            // 确保顺序连接，给服务端时间处理
            sleep(i * 0.8);
            
            Connector connector(true);
            if (connector.Connect("127.0.0.1", 9998)) {
                std::cout << "[Client" << (i+1) << "] 连接成功\n";
                sleep(2); // 保持连接一段时间
            }
        });
    }

    // 循环 Poll 直到所有客户端都连接
    int poll_count = 0;
    while (client_count < expected_clients && poll_count < 10) {
        EventLoop::ChannelList active_channels;
        loop.Poll(&active_channels, 500);
        
        for (auto ch : active_channels) {
            ch->HandleEvent();
        }
        poll_count++;
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
    std::cout << "        Acceptor + Connector 测试        \n";
    std::cout << "           使用 Channel + EventLoop       \n";
    std::cout << "========================================\n";

    TestAcceptorConnectorEcho();

    TestMultiClientWithAcceptor();

    std::cout << "\n========================================\n";
    std::cout << "            All Tests Completed         \n";
    std::cout << "========================================\n";

    return 0;
}
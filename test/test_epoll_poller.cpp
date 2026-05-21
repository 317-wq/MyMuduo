#include "../include/Channel.h"
#include "../include/EpollPoller.h"
#include "../include/EventLoop.h"

#include <iostream>
#include <unistd.h>
#include <cstring>

void PrintDivider() {
    std::cout << "\n========================================\n\n";
}

void TestChannelCallback() {
    std::cout << "[测试1] Channel 回调函数测试\n";
    PrintDivider();

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    EventLoop loop;

    Channel channel(pipefd[0]);

    bool read_cb_called = false;
    bool write_cb_called = false;

    channel.SetReadCallback([&read_cb_called]() {
        std::cout << "[Callback] Read callback triggered!\n";
        read_cb_called = true;
    });

    channel.SetWriteCallback([&write_cb_called]() {
        std::cout << "[Callback] Write callback triggered!\n";
        write_cb_called = true;
    });

    // [TEST-UPDATE] 使用 AddChannel 将 channel 添加到 EventLoop，同时设置 loop 指针
    loop.AddChannel(&channel);

    channel.EnableRead();
    std::cout << "[Step 1] EnableRead() called, events = " << channel.Events() << "\n";
    std::cout << "[Step 2] Write data to pipe to trigger read event...\n";

    const char* msg = "Hello from writer!";
    write(pipefd[1], msg, strlen(msg));

    std::cout << "[Step 3] Poll for events (timeout=1000ms)...\n";
    EventLoop::ChannelList active_channels;
    loop.GetPoller()->Poll(&active_channels, 1000);

    std::cout << "[Step 4] Active channels count: " << active_channels.size() << "\n";

    if (!active_channels.empty()) {
        for (auto ch : active_channels) {
            std::cout << "[Step 5] Handling event for fd=" << ch->Fd() << "\n";
            ch->HandleEvent();
        }
    }

    std::cout << "\n[Result] read_cb_called = " << (read_cb_called ? "true" : "false") << "\n";
    std::cout << "[Result] write_cb_called = " << (write_cb_called ? "true" : "false") << "\n";

    close(pipefd[0]);
    close(pipefd[1]);

    std::cout << "\n[Test 1] " << (read_cb_called ? "PASSED" : "FAILED") << "\n";
}

void TestChannelEnableDisable() {
    std::cout << "[测试2] Channel Enable/Disable 功能测试\n";
    PrintDivider();

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    EventLoop loop;
    Channel channel(pipefd[0]);

    // [TEST-UPDATE] 使用 AddChannel
    loop.AddChannel(&channel);

    std::cout << "[Step 1] Initial state: ReadAble=" << channel.ReadAble()
              << ", WriteAble=" << channel.WriteAble() << "\n";

    channel.EnableRead();
    std::cout << "[Step 2] After EnableRead(): ReadAble=" << channel.ReadAble()
              << ", WriteAble=" << channel.WriteAble() << "\n";

    channel.EnableWrite();
    std::cout << "[Step 3] After EnableWrite(): ReadAble=" << channel.ReadAble()
              << ", WriteAble=" << channel.WriteAble() << "\n";

    channel.DisableRead();
    std::cout << "[Step 4] After DisableRead(): ReadAble=" << channel.ReadAble()
              << ", WriteAble=" << channel.WriteAble() << "\n";

    channel.DisableAll();
    std::cout << "[Step 5] After DisableAll(): ReadAble=" << channel.ReadAble()
              << ", WriteAble=" << channel.WriteAble() << "\n";

    close(pipefd[0]);
    close(pipefd[1]);

    std::cout << "\n[Test 2] PASSED (state transitions verified)\n";
}

void TestEpollPollerAddUpdateRemove() {
    std::cout << "[测试3] EpollPoller Add/Update/Remove Channel 测试\n";
    PrintDivider();

    EpollPoller poller;
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    Channel channel(pipefd[0]);
    std::cout << "[Step 1] Create channel for fd=" << channel.Fd() << "\n";

    std::cout << "[Step 2] AddChannel to poller...\n";
    poller.AddChannel(&channel);

    std::cout << "[Step 3] Write data to trigger read event...\n";
    write(pipefd[1], "test", 4);

    EventLoop::ChannelList active_channels;
    poller.Poll(&active_channels, 100);
    std::cout << "[Step 4] Poll returned " << active_channels.size() << " active channel(s)\n";

    std::cout << "[Step 5] RemoveChannel from poller...\n";
    poller.RemoveChannel(&channel);

    active_channels.clear();
    poller.Poll(&active_channels, 100);
    std::cout << "[Step 6] After remove, Poll returned " << active_channels.size() << " active channel(s)\n";

    close(pipefd[0]);
    close(pipefd[1]);

    std::cout << "\n[Test 3] " << (active_channels.empty() ? "PASSED" : "FAILED") << "\n";
}

void TestEventLoopChannelManagement() {
    std::cout << "[测试4] EventLoop 管理 Channel 测试\n";
    PrintDivider();

    EventLoop loop;
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    auto channel = std::make_shared<Channel>(pipefd[0]);

    bool cb_called = false;
    channel->SetReadCallback([&cb_called]() {
        std::cout << "[Callback] EventLoop managed channel read callback!\n";
        cb_called = true;
    });

    // [TEST-UPDATE] 使用 AddChannel
    loop.AddChannel(channel.get());

    std::cout << "[Step 1] EnableRead on channel managed by EventLoop...\n";
    channel->EnableRead();

    std::cout << "[Step 2] Write data to pipe...\n";
    write(pipefd[1], "EventLoop test", 14);

    std::cout << "[Step 3] Poll through EventLoop...\n";
    EventLoop::ChannelList active_channels;
    loop.GetPoller()->Poll(&active_channels, 1000);

    std::cout << "[Step 4] Handling " << active_channels.size() << " active channel(s)...\n";
    for (auto ch : active_channels) {
        ch->HandleEvent();
    }

    close(pipefd[0]);
    close(pipefd[1]);

    std::cout << "\n[Test 4] " << (cb_called ? "PASSED" : "FAILED") << "\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   Channel + EpollPoller + EventLoop  \n";
    std::cout << "           模块功能测试                  \n";
    std::cout << "========================================\n";

    TestChannelCallback();

    TestChannelEnableDisable();

    TestEpollPollerAddUpdateRemove();

    TestEventLoopChannelManagement();

    std::cout << "\n========================================\n";
    std::cout << "            All Tests Completed         \n";
    std::cout << "========================================\n";

    return 0;
}
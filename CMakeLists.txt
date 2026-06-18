cmake_minimum_required(VERSION 3.10)

project(MyMuduo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# 添加头文件查询
include_directories(${PROJECT_SOURCE_DIR}/include)

# 原有 main 可执行文件
# add_executable(main test/main.cpp)
# add_executable(test_regex test/test_regex.cpp)
# add_executable(test_buffer 
#     test/test_buffer.cpp
#     src/Buffer.cpp
# )

# add_executable(test_log
#     test/test_log.cpp
#     src/Log.cpp
# )

# add_executable(test_inetaddress
#     test/test_inetaddress.cpp
#     src/InetAddress.cpp
# )

# add_executable(test_socket
#     test/test_socket.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
# )

# add_executable(test_socket_client
#     test/test_socket_client.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
# )

# add_executable(test_acceptor
#     test/test_acceptor.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
#     src/Acceptor.cpp
# )

# add_executable(test_acceptor_client
#     test/test_acceptor_client.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
#     src/Acceptor.cpp
# )

# add_executable(test_connector
#     test/test_connector.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
#     src/Acceptor.cpp
#     src/Connector.cpp
# )

# add_executable(test_connector_client
#     test/test_connector_client.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
#     src/Acceptor.cpp
#     src/Connector.cpp
# )

# # [TEST-ADD] Channel + EpollPoller + EventLoop 模块测试
# add_executable(test_epoll_poller
#     test/test_epoll_poller.cpp
#     src/Channel.cpp
#     src/Poller.cpp
#     src/EpollPoller.cpp
#     src/EventLoop.cpp
# )

# # [TEST-ADD] TCP服务端-客户端测试
# add_executable(test_tcp_server
#     test/test_tcp_server.cpp
#     src/Channel.cpp
#     src/Poller.cpp
#     src/EpollPoller.cpp
#     src/EventLoop.cpp
#     src/Socket.cpp
#     src/InetAddress.cpp
#     src/Acceptor.cpp
#     src/Connector.cpp
# )

# # Logger 测试
# add_executable(log_test test/log_test.cpp)
# target_link_libraries(log_test pthread)

# add_executable(test_eventloop
#     test/test_eventloop.cpp
#     src/Poller.cpp
#     src/EventLoop.cpp
#     src/Channel.cpp
#     src/EpollPoller.cpp
# )
# target_link_libraries(test_eventloop pthread)

# 网络库源文件
set(NET_SRCS
    src/net/Poller.cpp
    src/net/EventLoop.cpp
    src/net/Channel.cpp
    src/net/EpollPoller.cpp
    src/net/TcpServer.cpp
    src/net/TcpConnection.cpp
    src/net/Socket.cpp
    src/net/InetAddress.cpp
    src/net/Acceptor.cpp
    src/net/Buffer.cpp
    src/net/EventLoopThread.cpp
    src/net/EventLoopThreadPool.cpp
    src/net/Timer.cpp
    src/net/TimerQueue.cpp
    src/net/TimeWheel.cpp
)

add_executable(test_server
    test/test_server.cpp
    ${NET_SRCS}
)
target_link_libraries(test_server pthread)
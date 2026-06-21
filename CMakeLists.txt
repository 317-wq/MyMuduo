cmake_minimum_required(VERSION 3.10)

project(MyMuduo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# 生成 compile_commands.json，供 clangd/IDE 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 添加头文件查询
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加第三方库 include 路径
include_directories(${PROJECT_SOURCE_DIR}/third_party)

# jsoncpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
include_directories(${JSONCPP_INCLUDE_DIRS})
link_directories(${JSONCPP_LIBRARY_DIRS})

# MySQL Connector/C++
include_directories(/usr/include)
link_directories(/usr/lib/x86_64-linux-gnu)

# gtest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

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
    src/net/Log.cpp
)

# 协议层源文件
set(PROTO_SRCS
    src/proto/Message.cpp
    src/proto/Codec.cpp
)

# 数据库层源文件
set(DB_SRCS
    src/db/Database.cpp
    src/db/UserDao.cpp
    src/db/ConnectionPool.cpp
)

# 业务层源文件
set(SERVICE_SRCS
    src/service/UserService.cpp
    src/service/ChatServer.cpp
    src/service/EmailSender.cpp
)

# 基础工具源文件
set(BASE_SRCS
    src/base/Crypto.cpp
)

# 原有 test_server
# add_executable(test_server
#     test/test_server.cpp
#     ${NET_SRCS}
# )
# target_link_libraries(test_server pthread)

# 协议层测试 (gtest)
# add_executable(test_proto
#     test/test_proto.cpp
#     ${PROTO_SRCS}
#     src/net/Buffer.cpp
# )
# target_link_libraries(test_proto
#     pthread
#     ${JSONCPP_LIBRARIES}
#     GTest::Main
# )

# 数据库 + 用户服务集成测试
# add_executable(test_db
#     test/test_db.cpp
#     ${NET_SRCS}
#     ${PROTO_SRCS}
#     ${DB_SRCS}
#     ${SERVICE_SRCS}
#     ${BASE_SRCS}
# )
# target_link_libraries(test_db
#     pthread
#     ${JSONCPP_LIBRARIES}
#     mysqlcppconn
#     crypto
#     curl
# )

# 聊天室服务端
add_executable(chat_server
    examples/chat_server/main.cpp
    ${NET_SRCS}
    ${PROTO_SRCS}
    ${DB_SRCS}
    ${SERVICE_SRCS}
    ${BASE_SRCS}
)
target_link_libraries(chat_server
    pthread
    ${JSONCPP_LIBRARIES}
    mysqlcppconn
    crypto
    curl
)

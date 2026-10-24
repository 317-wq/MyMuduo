cmake_minimum_required(VERSION 3.10)

project(MyMuduo)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--copy-dt-needed-entries")

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
include_directories(/usr/include /usr/local/include)
link_directories(/usr/lib/x86_64-linux-gnu /usr/local/lib)

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
    src/db/FriendDao.cpp
    src/db/PrivateMessageDao.cpp
)

# 业务层源文件
set(SERVICE_SRCS
    src/service/UserService.cpp
    src/service/ChatServer.cpp
    src/service/EmailSender.cpp
    src/service/FriendService.cpp
)

# 基础工具源文件
set(BASE_SRCS
    src/base/Crypto.cpp
)

# 缓存层源文件
set(CACHE_SRCS
    src/cache/RedisCache.cpp
    src/cache/RedisDao.cpp
)

# 协议层测试 (gtest)
add_executable(test_proto
    test/test_proto.cpp
    ${PROTO_SRCS}
    src/net/Buffer.cpp
)
target_link_libraries(test_proto
    pthread
    ${JSONCPP_LIBRARIES}
    GTest::Main
)

# TCP 连接生命周期回归测试（B01-B04）
add_executable(test_tcp_lifecycle
    test/test_tcp_lifecycle.cpp
    ${NET_SRCS}
)
target_link_libraries(test_tcp_lifecycle
    pthread
    GTest::Main
)

# 服务端启动边界测试（B09）
add_executable(test_server_boundaries
    test/test_server_boundaries.cpp
    ${NET_SRCS}
)
target_link_libraries(test_server_boundaries
    pthread
    GTest::Main
)

# Redis 连接池测试（验证 redis++ 内置连接池）
add_executable(test_redis_pool
    test/test_redis_pool.cpp
)
target_link_libraries(test_redis_pool
    pthread
    /usr/local/lib/libredis++.so
    hiredis
    GTest::Main
)



# Redis DAO 数据访问层测试 (gtest)
add_executable(test_redis_dao
    test/test_redis_dao.cpp
    src/cache/RedisDao.cpp
)
target_link_libraries(test_redis_dao
    pthread
    /usr/local/lib/libredis++.so
    hiredis
    GTest::Main
)

# RedisCache 异步执行器测试
add_executable(test_redis_cache
    test/test_redis_cache.cpp
    ${NET_SRCS}
    src/cache/RedisCache.cpp
)
target_link_libraries(test_redis_cache
    pthread
    /usr/local/lib/libredis++.so
    hiredis
    GTest::Main
)

# 用户服务 + Redis 集成测试
add_executable(test_user_service_redis
    test/test_user_service_redis.cpp
    ${NET_SRCS}
    ${DB_SRCS}
    src/service/UserService.cpp
    src/service/EmailSender.cpp
    src/base/Crypto.cpp
    src/cache/RedisCache.cpp
    src/cache/RedisDao.cpp
)
target_link_libraries(test_user_service_redis
    pthread
    mysqlcppconn
    crypto
    curl
    /usr/local/lib/libredis++.so
    hiredis
    GTest::Main
)

# 好友服务 + Redis 集成测试
add_executable(test_friend_service
    test/test_friend_service.cpp
    ${NET_SRCS}
    ${DB_SRCS}
    src/service/FriendService.cpp
    src/cache/RedisCache.cpp
    src/cache/RedisDao.cpp
)
target_link_libraries(test_friend_service
    pthread
    mysqlcppconn
    /usr/local/lib/libredis++.so
    hiredis
    GTest::Main
)
# 好友 DAO 测试 (gtest)
add_executable(test_friend_dao
    test/test_friend_dao.cpp
    ${DB_SRCS}
    ${NET_SRCS}
)
target_link_libraries(test_friend_dao
    pthread
    mysqlcppconn
    GTest::Main
)

# 聊天室服务端
add_executable(chat_server
    examples/chat_server/main.cpp
    examples/chat_server/AppConfig.cpp
    ${NET_SRCS}
    ${PROTO_SRCS}
    ${DB_SRCS}
    ${SERVICE_SRCS}
    ${BASE_SRCS}
    ${CACHE_SRCS}
)
target_link_libraries(chat_server
    pthread
    ${JSONCPP_LIBRARIES}
    mysqlcppconn
    crypto
    curl
    /usr/local/lib/libredis++.so
)

# ============ 压测目标 ============

# HTTP Echo 压测服务端 (配合 wrk 使用)
add_executable(echo_bench
    benchmark/echo_server.cpp
    ${NET_SRCS}
)
target_link_libraries(echo_bench
    pthread
)

# TCP 原生协议压测客户端
add_executable(tcp_bench
    benchmark/tcp_bench.cpp
)
target_link_libraries(tcp_bench
    pthread
)

# ============ CTest ============
# 只注册不依赖 MySQL/Redis 的自包含测试；DB/Redis 集成测试仍按需手动运行。
add_test(NAME proto COMMAND test_proto)
add_test(NAME tcp_lifecycle COMMAND test_tcp_lifecycle)
add_test(NAME server_boundaries COMMAND test_server_boundaries)

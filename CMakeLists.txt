cmake_minimum_required(VERSION 3.10)

project(MyMuduo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# 添加头文件查询
include_directories(${PROJECT_SOURCE_DIR}/include)

# 原有 main 可执行文件
# add_executable(main test/main.cpp)
# add_executable(test_regex test/test_regex.cpp)
add_executable(test_buffer 
    test/test_buffer.cpp
    src/Buffer.cpp
)

add_executable(test_log
    test/test_log.cpp
    src/Log.cpp
)

# # Logger 测试
# add_executable(log_test test/log_test.cpp)
# target_link_libraries(log_test pthread)

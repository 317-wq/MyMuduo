cmake_minimum_required(VERSION 3.10)

project(MyMuduo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# 原有 main 可执行文件
add_executable(main test/main.cpp)
add_executable(test_regex test/test_regex.cpp)


# # Logger 测试
# add_executable(log_test test/log_test.cpp)
# target_link_libraries(log_test pthread)

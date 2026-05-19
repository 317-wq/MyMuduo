#pragma once

/*
    单独封装网络结构体地址信息sockaddr_in
*/

#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <cstring>

class InetAddress{
private:
    sockaddr_in _addr; // 网络地址信息

private:
    using u16 = uint16_t;
    // static const u16 DEFAULT_PORT = 8888;
public:
    // 空初始化
    InetAddress();

    // port初始化，服务端 -> 禁止编译器进行隐式类型化转换
    explicit InetAddress(u16 port);

    // ip:port初始化，客户端，提取客户端信息
    InetAddress(const std::string &ip, u16 port);

    // 获取原始地址
    sockaddr* Addr();

    const sockaddr* Addr() const;

    // 获取地址长度
    socklen_t Length() const;

    // 获取端口号
    u16 Port() const;

    // 获取字符串地址
    std::string Ip() const;

    ~InetAddress(){}
};
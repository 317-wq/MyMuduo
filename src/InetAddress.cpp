#include "../include/InetAddress.h"

// 空初始化
InetAddress::InetAddress() { memset(&_addr, 0, sizeof _addr); }

// port初始化，服务端 -> 禁止编译器进行隐式类型化转换
InetAddress::InetAddress(u16 port){
    memset(&_addr, 0, sizeof _addr);
    _addr.sin_family = AF_INET;
    _addr.sin_port = htons(port);
    _addr.sin_addr.s_addr = INADDR_ANY; // 绑定所有网卡
}

// ip:port初始化，客户端，提取客户端信息
InetAddress::InetAddress(const std::string &ip, u16 port){
    memset(&_addr, 0, sizeof _addr);
    _addr.sin_family = AF_INET;
    _addr.sin_port = htons(port);

    inet_pton(AF_INET, ip.c_str(), &_addr.sin_addr);
}

// 获取原始地址信息
sockaddr *InetAddress::Addr() { return (sockaddr*)&_addr; }

const sockaddr *InetAddress::Addr() const { return (sockaddr*)&_addr; }

// 获取地址长度
socklen_t InetAddress::Length() const{ return sizeof(_addr); }

// 获取端口号
InetAddress::u16 InetAddress::Port() const{ return ntohs(_addr.sin_port); }

// 获取字符串地址
std::string InetAddress::Ip() const{
    char ips[32]; // 一般情况下，ipv4协议的话，255.255.255.255，最多开16位就行了
    inet_ntop(AF_INET, &_addr.sin_addr, ips, sizeof ips);
    return std::string(ips);
}
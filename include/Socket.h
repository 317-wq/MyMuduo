#pragma once

/*
    管理fd文件描述符的生命周期(RAII)
    封装常用的socket套接字操作
    设置socket属性
*/

#include <unistd.h>
#include <memory>
#include <fcntl.h>

#include "NoCopy.h"
#include "InetAddress.h"

class Socket : public NoCopy {
private:
    int _fd;
public:
    using Ptr = std::shared_ptr<Socket>;

    // 初始化列表
    Socket();

    // 直接用fd构造
    explicit Socket(int fd);

    // 绑定地址信息
    bool Bind(const InetAddress& addr);

    // 监听连接
    bool Listen();

    // 获取新连接
    int Accept();

    // 客户端向服务端发起连接请求
    bool Connect();

    // 获取文件描述符
    int Fd() const;

    // 关闭套接字
    void Close();

    // 端口复用
    bool SetReusePort();

    // 地址复用
    bool SetReuseAddr();

    // 设置非阻塞
    bool SetNonBlock();

    // 析构
    ~Socket();
};
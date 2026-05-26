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
private:
    using u16 = uint16_t;
    static const int BACK_LOG = 10; // 最大连接数量

public:
    using Ptr = std::shared_ptr<Socket>;

    // 初始化列表  
    Socket();

    // 直接用fd构造
    explicit Socket(int fd);

    // 绑定地址信息
    bool Bind(const InetAddress& addr);

    bool Bind(u16 port);

    // 监听连接
    bool Listen(int backlog = BACK_LOG);

    // 获取新连接
    int Accept(InetAddress *client);
    int Accept(std::string *client_ip, Socket::u16 *client_port);

    // 客户端向服务端发起连接请求
    bool Connect(const InetAddress& server);

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
#pragma once

/*
    封装套接字函数：负责fd的生命周期管理

    EINTR：系统调用被信号中断，不是错误，后续重新recv等。continue
    (EWOULDBLOCK)EAGAIN：在非阻塞下，当前没有数据可读，常见的退出条件，ET木模式下

*/

#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <memory>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include "NoCopy.h"
#include "Log.h"

enum SOCK_ERR{
    SOCKET_ERR = 1,
    BIND_ERR,
    SETSOCKOPT_ERR,
    LISTEN_ERR,
    CONNECT_ERR
};

// 最大已成功连接的最大队列数
const int BACK_LOG = 10;

class Sock : public NoCopy
{
private:
    int _listen_fd;

private:
    using u16 = uint16_t;

private:
    // 关闭文件描述符
    void Close() const{
        if(_listen_fd > 0)
            close(_listen_fd);
    }

    // 创建文件描述符
    void Socket()
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        // 失败
        if(fd < 0){
            LOG_FATAL("errno=%d, errstr=%s", errno, strerror(errno));
            exit(SOCKET_ERR);
        }
        _listen_fd = fd;
        // 成功
        LOG_INFO("socket is success, _listen_fd=%d", _listen_fd);
    }
    
public:
    // 智能指针管理
    using Ptr = std::shared_ptr<Sock>;

    Sock()
        :_listen_fd(-1)
    {
        Socket();
    }

    // 获取文件描述符
    int Fd() const{ return _listen_fd; }

    // 允许端口，地址复用
    void SetSockOpt(){
        int opt = 1;
        // ip复用
        if(setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            LOG_WARNING("errno=%d, errstr=%s", errno, strerror(errno));
            exit(SETSOCKOPT_ERR);
        }
        // port复用
        opt = 1;
        if(setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt)){
            LOG_WARNING("errno=%d, errstr=%s", errno, strerror(errno));
            exit(SETSOCKOPT_ERR);
        }

        // 设置成功
        LOG_INFO("setsockopt is success...");
    }

    // 绑定地址信息
    void Bind(u16 port) const
    {
        sockaddr_in local;
        memset(&local, 0, sizeof local);

        local.sin_family = AF_INET;
        local.sin_port = htons(port);
        local.sin_addr.s_addr = INADDR_ANY; // 绑定所有网卡

        if(bind(_listen_fd, (const sockaddr*)&local, sizeof local) < 0){
            LOG_FATAL("errno=%d, errstr=%s", errno, strerror(errno));
            exit(BIND_ERR);
        }
        LOG_INFO("bind is success");
    }

    // 监听连接 -> 可以设置最大的一次性监听数量
    void Listen(int max_listen = BACK_LOG) const
    {
        if(listen(_listen_fd, max_listen) < 0){
            LOG_FATAL("errno=%d, errstr=%s", errno, strerror(errno));
            exit(LISTEN_ERR);
        }
        LOG_INFO("listen is success");
    }

    // 获取新连接
    int Accept(std::string& client_ip, u16& client_port) const
    {
        sockaddr_in peer;
        memset(&peer, 0, sizeof peer);
        socklen_t len = sizeof peer;
        int fd = accept(_listen_fd, (sockaddr*)&peer, &len);

        if(fd < 0){
            LOG_WARNING("accept is fail, errno=%d, errstr=%s", errno, strerror(errno));
            return -1;
        }

        // 取出peer中的ip port信息
        client_port = ntohs(peer.sin_port);
        char ips[32]; // 点分十进制
        std::string tip = inet_ntop(AF_INET, &peer.sin_addr, ips, sizeof ips);
        if(tip.empty()){
            LOG_WARNING("inet_ntop is fail, errno=%d, errstr=%s", errno, strerror(errno));
            return -1;
        }
        client_ip = tip;
        LOG_INFO("get a new link, ip=%s, port=%d", client_ip.c_str(), client_port);
        return fd;
    }

    // 客户端向服务器发起连接
    bool Connect(const std::string& serve_ip, u16 serve_port) const{
        sockaddr_in serve;
        memset(&serve, 0, sizeof serve);

        serve.sin_family = AF_INET;
        serve.sin_port = htons(serve_port);
        inet_pton(AF_INET, serve_ip.c_str(), &serve.sin_addr);

        if(connect(_listen_fd, (const sockaddr*)&serve, sizeof serve) < 0){
            LOG_WARNING("errno=%d, errstr=%s", errno, strerror(errno));
            return false;
        }

        LOG_INFO("connect is success");
        return true;
    }

    // 设置非阻塞用于ET模式[epoll属性]
    void SetNonBlock(){
        // 得到文件状态标志位
        int status = fcntl(_listen_fd, F_GETFL);
        if(status < 0){
            LOG_FATAL("errno=%d, strerr=%s", errno, strerror(errno));
        }
        // 设置非阻塞
        fcntl(_listen_fd, F_SETFL, status | O_NONBLOCK);
        LOG_INFO("非阻塞创建成功");
    }

    ~Sock(){
        Close();
    }

    // 创建服务端连接
    void CreateServer(u16 port, int max_listen = BACK_LOG){
        SetNonBlock(); // 设置非阻塞
        SetSockOpt(); // 复用ip，port
        Bind(port); // 绑定地址信息
        Listen(max_listen); // 监听连接
    }

    // 创建客户端连接
    void CreateClient(){

    }
};
#pragma once

// 实现一些套接字组件

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

enum SOCK_ERR{
    SOCKET_ERR = 1,
    BIND_ERR,
    SETSOCKOPT_ERR,
    LISTEN_ERR,
    CONNECT_ERR
};

// 最大已成功连接的最大队列数
const int BLOCK_LOG = 10;

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

    // 服务端函数
    void Socket()
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if(fd < 0){
            // log.Message(FATAL, "errno=%d, errstr=%s", errno, strerror(errno));
            exit(SOCKET_ERR);
        }
        _listen_fd = fd;
        // log.Message(INFO, "socket is success, _listen_fd=%d", _listen_fd);
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
    int Fd() const{
        return _listen_fd; 
    }

    // 允许端口复用
    void SetSockOpt(){
        int opt = 1;
        if(setsockopt(_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            // log.Message(WARNING, "errno=%d, errstr=%s", errno, strerror(errno));
            exit(SETSOCKOPT_ERR);
        }
        // log.Message(INFO, "setsockopt is success...");
    }

    void Bind(u16 port) const
    {
        sockaddr_in local;
        memset(&local, 0, sizeof local);

        local.sin_family = AF_INET;
        local.sin_port = htons(port);
        local.sin_addr.s_addr = INADDR_ANY;

        if(bind(_listen_fd, (const sockaddr*)&local, sizeof local) < 0){
            // log.Message(FATAL, "errno=%d, errstr=%s", errno, strerror(errno));
            exit(BIND_ERR);
        }
        // log.Message(INFO, "bind is success");
    }

    void Listen() const
    {
        if(listen(_listen_fd, BLOCK_LOG) < 0){
            // log.Message(FATAL, "errno=%d, errstr=%s", errno, strerror(errno));
            exit(LISTEN_ERR);
        }
        // log.Message(INFO, "listen is success");
    }

    // 连接成功 返回对应的文件描述符 后面线程进行服务
    int Accept(std::string& client_ip, u16& client_port) const
    {
        sockaddr_in peer;
        memset(&peer, 0, sizeof peer);
        socklen_t len = sizeof peer;
        int fd = accept(_listen_fd, (sockaddr*)&peer, &len);

        if(fd < 0){
            // log.Message(WARNING, "accept is fail, errno=%d, errstr=%s", errno, strerror(errno));
            return -1;
        }

        client_port = ntohs(peer.sin_port);
        char ips[32];
        std::string tip = inet_ntop(AF_INET, &peer.sin_addr, ips, sizeof ips);
        if(tip.empty()){
            // log.Message(WARNING, "inet_ntop is fail, errno=%d, errstr=%s", errno, strerror(errno));
            return -1;
        }
        client_ip = tip;
        // log.Message(INFO, "get a new link, ip=%s, port=%d", client_ip.c_str(), client_port);
        return fd;
    }

    // 客户端函数
    bool Connect(const std::string& serve_ip, u16 serve_port) const{
        sockaddr_in serve;
        memset(&serve, 0, sizeof serve);

        serve.sin_family = AF_INET;
        serve.sin_port = htons(serve_port);
        inet_pton(AF_INET, serve_ip.c_str(), &serve.sin_addr);

        if(connect(_listen_fd, (const sockaddr*)&serve, sizeof serve) < 0){
            // log.Message(WARNING, "errno=%d, errstr=%s", errno, strerror(errno));
            return false;
        }

        // log.Message(INFO, "connect is success");
        return true;
    }


    // 设置非阻塞用于ET模式
    void SetNonBlock(){
        // 得到文件状态标志位
        int status = fcntl(_listen_fd, F_GETFL);
        if(status < 0){
            // log.Message(FATAL, "errno=%d, strerr=%s", errno, strerror(errno));
        }
        // 设置非阻塞
        fcntl(_listen_fd, F_SETFL, status | O_NONBLOCK);
    }

    ~Sock(){
        Close();
    }
};
#include "../include/Socket.h"

// 初始化列表
Socket::Socket(): _fd(-1) {}

// 直接用fd构造
Socket::Socket(int fd): _fd(fd) {}

// 绑定地址信息
bool Socket::Bind(const InetAddress &addr) {
    bind(_fd, addr.Addr(), addr.Length());
}

// 监听连接
bool Socket::Listen() {

}

// 获取新连接
int Socket::Accept() {

}

// 客户端向服务端发起连接请求
bool Socket::Connect() {

}

// 获取文件描述符
int Socket::Fd() const { return _fd; }

// 关闭套接字
void Socket::Close() {
    if(_fd > 0){
        close(_fd);
        _fd = -1;
    }
}

// 端口复用
bool Socket::SetReusePort() {
    int opt = 1;
    return setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt) >= 0;
}

// 地址复用
bool Socket::SetReuseAddr() {
    int opt = 1;
    return setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) >= 0;
}

// 设置非阻塞
bool Socket::SetNonBlock() {
    int status = fcntl(_fd, F_GETFL);
    if(status < 0)
        return false;
    status |= O_NONBLOCK;
    return fcntl(_fd, F_SETFL, status) >= 0;
}

// 析构
Socket::~Socket() { Close(); }
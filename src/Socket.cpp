#include "../include/Socket.h"

// 初始化列表
Socket::Socket(): _fd(socket(AF_INET, SOCK_STREAM, 0)) {
    if(_fd < 0)
        perror("socket");
}

// 直接用fd构造
Socket::Socket(int fd): _fd(fd) {}

// 绑定地址信息
bool Socket::Bind(const InetAddress &addr) {
    return bind(_fd, addr.Addr(), addr.Length()) >= 0;
}

bool Socket::Bind(Socket::u16 port){
    InetAddress local(port);
    return bind(_fd, local.Addr(), local.Length());
}

// 监听连接
bool Socket::Listen(int backlog) {
    return listen(_fd, backlog) >= 0;
}

// 获取新连接 -> 输出型参数
int Socket::Accept(InetAddress *client) {
    while(true){
        // socklen_t len = client->Length();
        socklen_t len = sizeof(sockaddr_in);
        int fd = accept(_fd, client->Addr(), &len);

        if(fd >= 0)
            return fd; // 合法
        
        // fd = -1的时候，不一定是错误，在非阻塞环境下
        if(errno == EINTR)
            continue; // 信号中断。重试
        if(errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; // 现在没有新连接
        
        return -1; // 错误
    }
}

int Socket::Accept(std::string *client_ip, Socket::u16 *client_port) {
    InetAddress client;
    int fd = Accept(&client);
    if(fd > 0){
        // 合法的连接
        if(client_ip) *client_ip = client.Ip();
        if(client_port) *client_port = client.Port();
    }
    return fd;
}

// 客户端向服务端发起连接请求
bool Socket::Connect(const InetAddress &peer) {
    return connect(_fd, peer.Addr(), peer.Length()) >= 0;
}

// 获取文件描述符
int Socket::Fd() const { return _fd; }

// 关闭套接字
void Socket::Close() {
    if(_fd >= 0){
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
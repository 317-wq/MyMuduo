#pragma once

/*
    创建一个专门用于监听新连接的对象
*/

#include "Socket.h"

class Acceptor {
private:
    using u16 = uint16_t;
private:
    Socket::Ptr _listen_sock;
public:
    // 初始化监听对象 -> true为非阻塞
    Acceptor(const InetAddress &client, bool non_block = true);
    Acceptor(u16 port, bool non_block = true);

    // 返回新连接的文件描述符fd
    int Accept(InetAddress *client);
    int Accept(std::string *client_ip, u16 *client_port);
};
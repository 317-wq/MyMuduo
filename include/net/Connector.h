#pragma once

/*
    主动发起连接
*/

#include "net/Socket.h"

class Connector {
private:
    Socket::Ptr _conn_sock;
private:
    using u16 = uint16_t;
public:
    Connector(bool non_block = true);

    // 发起连接
    bool Connect(const InetAddress &server);
    bool Connect(const std::string &server_ip, u16 server_port);

    int Fd() const;
};
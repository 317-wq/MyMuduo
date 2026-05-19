#include "../include/Connector.h"

Connector::Connector(bool non_block)
    : _conn_sock(std::make_shared<Socket>()) 
{
    if(non_block)
        _conn_sock->SetNonBlock();
}

// 发起连接
bool Connector::Connect(const InetAddress &server){
    return _conn_sock->Connect(server);
}

bool Connector::Connect(const std::string &ip, u16 port){
    // return _conn_sock->Connect(InetAddress(ip, port));
    return Connect(InetAddress(ip, port));
}

int Connector::Fd() const { return _conn_sock->Fd(); }
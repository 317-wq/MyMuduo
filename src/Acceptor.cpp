#include "../include/Acceptor.h"

// 初始化监听对象
Acceptor::Acceptor(const InetAddress &client, bool non_block)
    : _listen_sock(std::make_shared<Socket>())
{
    _listen_sock->SetReuseAddr();
    _listen_sock->SetReusePort();
    if (non_block)
        _listen_sock->SetNonBlock();
    _listen_sock->Bind(client);
    _listen_sock->Listen();
}

Acceptor::Acceptor(Acceptor::u16 port, bool non_block)
    : Acceptor(InetAddress(port), non_block)
    {}

// 返回新连接的文件描述符fd
int Acceptor::Accept(InetAddress *client){
    return _listen_sock->Accept(client);
}

int Acceptor::Accept(std::string *client_ip, Acceptor::u16 *client_port){
    return _listen_sock->Accept(client_ip, client_port);
}

// 获取监听socket的fd
int Acceptor::Fd() const { 
    return _listen_sock->Fd(); 
}
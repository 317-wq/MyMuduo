#include "../include/TcpServer.h"
#include "../include/Log.h"

int main(){
    EventLoop loop;
    TcpServer server(&loop, 8080);
    server.SetMessageCallback([](TcpConnection::Ptr conn, Buffer* buffer){
        std::string str = buffer->RetrieveAllAsString();
        if(conn->Connected())
            conn->Send(str);
        // LOG_INFO("")
    });
    server.Start();
    loop.Loop();
    return 0;
}
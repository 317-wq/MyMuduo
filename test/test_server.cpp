#include "../include/TcpServer.h"

int main(){
    EventLoop loop;
    TcpServer server(&loop, 8080);
    server.Start();
    loop.Loop();
    return 0;
}
#include "../include/Socket.h"
#include <iostream>

int main() {
    Socket client;

    InetAddress server(
        "127.0.0.1",
        8888
    );

    if(!client.Connect(server)){
        perror("connect");
        return -1;
    }

    std::cout
        << "connect success"
        << std::endl;

    return 0;
}
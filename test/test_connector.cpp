#include "../include/Acceptor.h"

#include <iostream>
#include <unistd.h>

int main(){

    Acceptor acceptor(
        8888,
        false
    );

    std::cout
        << "server start"
        << std::endl;

    while(true){

        std::string ip;

        uint16_t port;

        int fd =
            acceptor.Accept(
                &ip,
                &port
            );

        if(fd > 0){

            std::cout
                << "new client : "
                << ip
                << ":"
                << port
                << std::endl;

            close(fd);
        }
    }

    return 0;
}
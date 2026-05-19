// #include "../include/Acceptor.h"

// #include <iostream>

// int main(){

//     Acceptor acceptor(
//         8888,
//         false
//     );

//     while(true){

//         InetAddress peer;

//         int fd =
//             acceptor.Accept(
//                 &peer
//             );

//         if(fd > 0){

//             std::cout
//                 << peer.Ip()
//                 << ":"
//                 << peer.Port()
//                 << std::endl;

//             close(fd);
//         }
//     }
// }

#include "../include/Acceptor.h"

#include <iostream>

int main(){

    Acceptor acceptor(
        8888,
        false
    );

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
                << ip
                << ":"
                << port
                << std::endl;

            close(fd);
        }
    }

    return 0;
}
// #include "../include/Socket.h"
// #include "../include/InetAddress.h"
// int main(){
//     Socket::Ptr ptr = std::make_shared<Socket>();

//     InetAddress local(8888);

//     ptr->SetReuseAddr();
//     ptr->SetReusePort();

//     if(!ptr->Bind(local)){
//         perror("bind");
//         return -1;
//     }

//     if(!ptr->Listen()){
//         perror("listen");
//         return -1;
//     }

//     return 0;
// }

// #include "../include/Socket.h"
// #include <iostream>

// int main() {
//     Socket server;

//     server.SetReuseAddr();
//     server.SetReusePort();

//     InetAddress local(8888);

//     if(!server.Bind(local)){
//         perror("bind");
//         return -1;
//     }

//     if(!server.Listen()){
//         perror("listen");
//         return -1;
//     }

//     std::cout
//         << "server start : "
//         << local.Port()
//         << std::endl;

//     while(true);

//     return 0;
// }

// #include "../include/Socket.h"
// #include <iostream>

// int main() {
//     Socket server;

//     server.SetReuseAddr();
//     server.SetReusePort();

//     InetAddress local(8888);

//     server.Bind(local);
//     server.Listen();

//     while(true){

//         InetAddress peer;

//         int fd = server.Accept(&peer);

//         if(fd > 0){

//             std::cout
//                 << "new client : "
//                 << peer.Ip()
//                 << ":"
//                 << peer.Port()
//                 << std::endl;

//             close(fd);
//         }
//     }

//     return 0;
// }

// #include "../include/Socket.h"
// #include <iostream>
// int main()
// {
//     Socket server;

//     server.SetNonBlock();

//     server.Bind(
//         InetAddress(8888));

//     server.Listen();

//     while (true)
//     {

//         InetAddress peer;

//         int fd = server.Accept(&peer);

//         if (fd == 0)
//         {

//             std::cout
//                 << "no client"
//                 << std::endl;

//             sleep(1);

//             continue;
//         }

//         if (fd > 0)
//         {

//             std::cout
//                 << "accept success"
//                 << std::endl;
//         }
//     }
// }

#include "../include/Socket.h"
#include <iostream>
void Test(){

    Socket s;

    std::cout
        << s.Fd()
        << std::endl;

}

int main(){

    Test();

    while(true);

}
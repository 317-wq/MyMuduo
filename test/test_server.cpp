#include "../include/TcpServer.h"
#include "../include/EventLoopThreadPool.h"
#include "../include/Log.h"

// int main(){
//     EventLoop loop;
//     TcpServer server(&loop, 8080);
//     server.SetMessageCallback([](TcpConnection::Ptr conn, Buffer* buffer){
//         std::string str = buffer->RetrieveAllAsString();
//         if(conn->Connected())
//             conn->Send(str);
//         // LOG_INFO("")
//     });
//     server.Start();
//     loop.Loop();
//     return 0;
// }

int main()
{
    EventLoop loop;

    TcpServer server(
        &loop,
        8080,
        4);

    server.SetMessageCallback(
        [](auto conn, Buffer *buf)
        {
            auto str = buf->RetrieveAllAsString();
            conn->Send(str);
        });

    server.Start();

    loop.Loop();
    //     EventLoopThread t;

    // EventLoop* loop =
    //     t.StartLoop();

    // if(loop)
    // {
    //     std::cout
    //     << "create loop success"
    //     << std::endl;
    // }

    // while(true)
    // {
    //     sleep(1);
    // }

    return 0;
}
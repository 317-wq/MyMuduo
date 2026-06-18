// #include "../include/TcpServer.h"
// #include "../include/EventLoopThreadPool.h"
// #include "../include/Log.h"
// #include "../include/Timer.h"
// #include "../include/TimerQueue.h"

// // int main(){
// //     EventLoop loop;
// //     TcpServer server(&loop, 8080);
// //     server.SetMessageCallback([](TcpConnection::Ptr conn, Buffer* buffer){
// //         std::string str = buffer->RetrieveAllAsString();
// //         if(conn->Connected())
// //             conn->Send(str);
// //         // LOG_INFO("")
// //     });
// //     server.Start();
// //     loop.Loop();
// //     return 0;
// // }

// // int main()
// // {
// //     EventLoop loop;

// //     TcpServer server(
// //         &loop,
// //         8080,
// //         4);

// //     server.SetMessageCallback(
// //         [](auto conn, Buffer *buf)
// //         {
// //             auto str = buf->RetrieveAllAsString();
// //             conn->Send(str);
// //         });

// //     server.Start();

// //     loop.Loop();
// //     //     EventLoopThread t;

// //     // EventLoop* loop =
// //     //     t.StartLoop();

// //     // if(loop)
// //     // {
// //     //     std::cout
// //     //     << "create loop success"
// //     //     << std::endl;
// //     // }

// //     // while(true)
// //     // {
// //     //     sleep(1);
// //     // }

// //     return 0;
// // }

// int main()
// {
//     // using Clock =
//     //     Timer::Clock;

//     // Timer t(
//     //     Clock::now() +
//     //         std::chrono::seconds(
//     //             3),

//     //     []
//     //     {
//     //         std::cout
//     //             << "timer run"
//     //             << std::endl;
//     //     }, true, std::chrono::seconds(1));

//     // while (true)
//     // {
//     //     if (
//     //         t.Expired())
//     //     {
//     //         t.Run();

//     //         t.Restart();
//     //     }

//     //     // std::this_thread::
//     //     //     sleep_for(
//     //     //         std::chrono::
//     //     //             milliseconds(
//     //     //                 50));
//     // }

//     // return 0;
//     EventLoop loop;
//     loop.RunAfter(
//         5,
//         []
//         {
//             std::cout
//                 << "after 5"
//                 << std::endl;
//         });
//     loop.RunEvery(
//         1,
//         []
//         {
//             std::cout
//                 << "tick"
//                 << std::endl;
//         });

//     loop.Loop();
// }

#include "net/EventLoop.h"
#include "net/TimeWheel.h"

#include <iostream>

int main()
{

    EventLoop loop;

    /*
        5秒超时
    */

    TimeWheel wheel(
        &loop,
        5);

    /*
        超时回调
    */

    wheel.SetTimeoutCallback(
        [](int fd)
        {
            std::cout
                << "timeout fd: "
                << fd
                << std::endl;
        });

    /*
        插入连接
    */

    wheel.Insert(1);
    wheel.Insert(2);
    wheel.Insert(3);

    std::cout
        << "insert fd 1 2 3"
        << std::endl;

    /*
        2秒后刷新

        相当于：

            重新续命
    */

    loop.RunEvery(
        1,
        [&wheel]
        {
            wheel.Refresh(1);
        });

    /*
        8秒后：

            应该真正超时
    */

    loop.RunAfter(
        8,

        []
        {
            std::cout
                << "8 sec passed"
                << std::endl;
        });

    loop.Loop();

    return 0;
}
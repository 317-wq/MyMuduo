// #include "../include/EventLoop.h"
// #include <iostream>

// int main()
// {
//     EventLoop loop;

//     std::cout
//         << "loop start..."
//         << std::endl;

//     loop.Loop();

//     return 0;
// }

#include "../include/EventLoop.h"

#include <iostream>
#include <thread>
#include <chrono>

int main()
{
    EventLoop loop;

    std::thread t(
        [&]()
        {
            std::this_thread
                ::sleep_for(
                    std::chrono::seconds(2)
                );

            std::cout
                << "push task"
                << std::endl;

            loop.QueueInLoop(
                []()
                {
                    std::cout
                        << "task run"
                        << std::endl;
                }
            );
        }
    );

    loop.Loop();

    t.join();

    return 0;
}
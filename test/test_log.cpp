#include "../include/Log.h"

using namespace std;

int main()
{
    LOG_INFO("server start port=%d", 8080);

    LOG_DEBUG("fd=%d create success", 5);

    LOG_WARNING("connection timeout id=%d", 100);

    LOG_ERROR("socket create fail errno=%d", errno);

    LOG_FATAL("poller crash");
    return 0;
}
#include "../include/EventLoop.h"
#include "../include/EpollPoller.h"

EventLoop::EventLoop()
    :_running(false), _poller(std::make_shared<EpollPoller>())
{}

void EventLoop::UpdateChannel(Channel* channel){
    _poller->UpdateChannel(channel);
}

void EventLoop::AddChannel(Channel* channel){
    // 设置 Channel 的 loop 指针，并添加到 poller
    /*
        这边的loop变量一开始是nullptr，如果不先赋值的话，就会导致对空指针进行访问
        采用设计统一接口的方式，如果不这样设计的话，就是在channel里面将eventloop类
        设置成friend，允许对私有成员变量进行直接操作
    */
    channel->SetLoop(this);
    _poller->AddChannel(channel);
}

EventLoop::~EventLoop(){

}
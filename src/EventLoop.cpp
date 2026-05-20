#include "../include/EventLoop.h"

EventLoop::EventLoop()
    :_running(false), _poller(std::make_shared<Poller>())
{}

void EventLoop::UpdateChannel(Channel* channel){
    _poller->UpdateChannel();
}
EventLoop::~EventLoop(){

}
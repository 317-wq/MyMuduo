#include "../include/Channel.h"

Channel::Channel(int fd)
    : _fd(fd), _events(0), _revents(0), _loop(std::make_shared<EventLoop>())
{}

int Channel::Fd() const { return _fd; }
Channel::u32 Channel::Event() const { return _events; }
Channel::u32 Channel::Revent() const { return _revents; }

void Channel::SetREvent(Channel::u32 event) { _revents = event; }

void Channel::SetReadCallback(Channel::EventCallback read_cb) { _read_cb = std::move(read_cb); }
void Channel::SetWriteCallback(Channel::EventCallback write_cb) { _write_cb = std::move(write_cb); }
void Channel::SetErrorCallback(Channel::EventCallback error_cb) { _error_cb = std::move(error_cb); }
void Channel::SetCloseCallback(Channel::EventCallback close_cb) { _close_cb = std::move(close_cb); }
void Channel::SetEventCallback(Channel::EventCallback event_cb) { _event_cb = std::move(event_cb); }

void Channel::EnableRead() { 
    _events |= EPOLLIN;
    UpdateEvent();
}
void Channel::EnableWrite() {
    _events |= EPOLLOUT;
    UpdateEvent();
}

void Channel::DisableRead() {
    _events &= ~EPOLLIN; 
    UpdateEvent();
}

void Channel::DisableWrite() {
    _events &= ~EPOLLOUT;
    UpdateEvent();
}

void Channel::DisableAll() { _events = 0; }

bool Channel::ReadAble() const { return _events & EPOLLIN; }
bool Channel::WriteAble() const { return _events & EPOLLOUT; }

void Channel::HandleEvent(){
    if(_revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)){
        if(_read_cb)
            _read_cb();
    }

    if(_revents & EPOLLOUT){
        if(_write_cb)
            _write_cb();
    }

    if(_revents & EPOLLERR){
        if(_error_cb)
            _error_cb();
    }

    if(_revents & EPOLLHUP){
        if(_close_cb)
            _close_cb();
    }

    if(_event_cb)
        _event_cb();
}

// void Close();

// EventLoop来管理这些操作
void Channel::UpdateEvent(){
    _loop->Update();
}

void Channel::RemoveEvent(){
    _loop->Update();
}

Channel::~Channel() = default;
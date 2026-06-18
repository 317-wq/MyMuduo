#include "net/Channel.h"
#include "net/EventLoop.h"

// Channel::Channel(int fd)
//     : _fd(fd), _events(0), _revents(0), _loop(nullptr)
// {}

Channel::Channel(EventLoop* loop, int fd)
    : _fd(fd), _events(0), _revents(0), _loop(loop)
{}

int Channel::Fd() const { return _fd; }
Channel::u32 Channel::Events() const { return _events; }
Channel::u32 Channel::Revents() const { return _revents; }

void Channel::SetREvents(Channel::u32 event) { _revents = event; }

void Channel::SetReadCallback(Channel::EventCallback read_cb) { _read_cb = std::move(read_cb); }
void Channel::SetWriteCallback(Channel::EventCallback write_cb) { _write_cb = std::move(write_cb); }
void Channel::SetErrorCallback(Channel::EventCallback error_cb) { _error_cb = std::move(error_cb); }
void Channel::SetCloseCallback(Channel::EventCallback close_cb) { _close_cb = std::move(close_cb); }
void Channel::SetEventCallback(Channel::EventCallback event_cb) { _event_cb = std::move(event_cb); }

void Channel::EnableRead() { 
    _events |= EPOLLIN;
    Update();
}

void Channel::EnableWrite() {
    _events |= EPOLLOUT;
    Update();
}

void Channel::DisableRead() {
    _events &= ~EPOLLIN; 
    Update();
}

void Channel::DisableWrite() {
    _events &= ~EPOLLOUT;
    Update();
}

void Channel::DisableAll() { 
    _events = 0; 
    Update(); // 需要更新到内核
}

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

// EventLoop来管理这些操作
void Channel::Update(){
    // 添加空指针检查，避免未设置 loop 时崩溃
    if (_loop) {
        _loop->UpdateChannel(this);
    }
}

void Channel::Remove(){
    DisableAll();
    if(_loop){
        _loop->RemoveChannel(this);
    }
}

// 设置所属的 EventLoop
// void Channel::SetLoop(EventLoop *loop) { _loop = loop; }

Channel::~Channel() = default;
#include "net/EpollPoller.h"
#include "base/Exception.h"

// 判断监听对象是否存在hash表中
bool EpollPoller::ExistChannel(Channel* channel) const {
    return _channels.find(channel->Fd()) != _channels.end();
}


// 添加监听对象
void EpollPoller::AddChannel(Channel* channel){
    // 成功后再记录
    Update(EPOLL_CTL_ADD, channel);
    // 添加到hash表里面
    _channels[channel->Fd()] = channel;
}

void EpollPoller::Update(int op, Channel* channel){
    epoll_event ev;
    ev.events = channel->Events();
    // ev.data.ptr = channel.get(); // 获取裸指针
    ev.data.ptr = channel; // 保存指针
    int n = epoll_ctl(_epfd, op, channel->Fd(), &ev);
    if(n < 0)
        throw mymuduo::Exception::FromErrno("epoll_ctl failed");
}

EpollPoller::EpollPoller()
    :_events(INIT_EVENT)
{
    // 其他进程调用fork，exec的时候
    // 内核自动关闭epoll实例，避免epoll文件描述符泄露
    _epfd = epoll_create1(EPOLL_CLOEXEC);
    if(_epfd < 0){
        throw mymuduo::Exception::FromErrno("epoll_create1 failed");
    }
}

// 事件等待 + 存储活跃连接
void EpollPoller::Poll(ChannelList* active_channels, int timeout){
    int num = epoll_wait(_epfd, _events.data(), _events.size(), timeout);
    
    // 扩容
    int size = _events.size();
    if(num == size){
        size *= 2;
    }
    if(size != _events.size())
        _events.resize(size);
    
    if(num <= 0)
        return;
    
    // num个活跃连接，存储
    FillActiveChannels(active_channels, num);
}

// 更新监听对象
void EpollPoller::UpdateChannel(Channel* channel){
    if(ExistChannel(channel))
        Update(EPOLL_CTL_MOD, channel);
    else AddChannel(channel);
}

// 删除监听对象
void EpollPoller::RemoveChannel(Channel* channel){
    if(ExistChannel(channel)){
        Update(EPOLL_CTL_DEL, channel);
        _channels.erase(channel->Fd());
    }
}

EpollPoller::~EpollPoller(){
    if(_epfd >= 0){
        close(_epfd);
        _epfd = -1;
    }
}

void EpollPoller::FillActiveChannels(ChannelList* active_channels, int num){
    for(int i = 0; i < num; ++i){
        Channel* channel = static_cast<Channel*>(_events[i].data.ptr);
        // 多个触发事件设置，等待回来的事件都是就绪的触发事件
        // 后续就是对这些触发事件进行对应的回调处理
        channel->SetREvents(_events[i].events);
        active_channels->push_back(channel);
    }
}
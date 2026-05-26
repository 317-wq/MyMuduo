#pragma once

#include "NoCopy.h"
#include "Channel.h"
#include "Buffer.h"
#include "Socket.h"
#include <memory>
#include <functional>

// 一个accept返回的fd文件描述符对应一个连接connection

class TcpConnection : public NoCopy,
                      public std::enable_shared_from_this<TcpConnection>
{
public:
    // 无法自己一个人决定连接是否释放，要保证回调函数存在等，所以使用shared_ptr
    using Ptr = std::shared_ptr<TcpConnection>;
    using ConnectCallback = std::function<void(const Ptr&)>; // 连接事件回调
    using MessageCallback = std::function<void(const Ptr&)>; // 消息事件回调
    using CloseCallback = std::function<void(const Ptr&)>; // 通道关闭事件回调

private:
    // 读事件绑定
    void HandleRead();

    // 写事件绑定
    void HandleWrite();

    // 关闭连接事件绑定
    void HandleClose();

    // 错误事件绑定
    void HandleError();

private:
    EventLoop* _loop;
    std::unique_ptr<Socket> _socket; // 套接字对象管理
    std::unique_ptr<Channel> _channel; // 对象自己拥有
    Buffer _in_buffer; // msg -> inbuffer -> socket取
    Buffer _out_buffer; // socket输出到outbuffer，上层自己取出数据
    ConnectCallback _connect_cb; // 通知上层连接到来
    MessageCallback _message_cb; // 通知上层有数据
    CloseCallback _close_cb; // 通知上层连接关闭

public:
    TcpConnection(EventLoop* loop, int fd);

    int Fd() const;

    // 建立连接
    void ConnectEstablished();
    // 主动发送
    void Send(const std::string &str);
    // 销毁连接，内核中去除，hash里面删除
    void ConnectDestroyed();

    // 设置TcpConnection层的回调
    void SetConnectCallback(ConnectCallback cb);
    void SetMessageCallback(MessageCallback cb);
    void SetCloseCallback(CloseCallback cb);

    ~TcpConnection();
};
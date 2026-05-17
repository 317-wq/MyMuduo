#pragma once

/*
    实现缓冲区部分
*/

#include <cstddef>
#include <vector>

class Buffer{
private:
    static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;

private:
    // 写指针移动
    size_t MoveWritePos(size_t len) { _write_pos += len; }

    // 读指针移动
    size_t MoveReadPos(size_t len) { _read_pos += len; }

private:
    std::vector<char> _buffer; // 缓冲区
    size_t _read_pos; // 读指针位置
    size_t _write_pos; // 写指针位置

public:   
    Buffer()
    :_buffer(DEFAULT_BUFFER_SIZE)
    ,_read_pos(0)
    ,_write_pos(0)
    {}

    // 获取缓冲区大小
    size_t BufferSize();

    // 获取读指针当前位置
    size_t ReadPos();

    // 获取写指针当前位置
    size_t WritePos();

    // 可读数据区大小
    size_t ReadableSize();

    // 计算前置剩余空间大小
    size_t FrontRemainSize();

    // 计算后置剩余空间大小
    size_t AfterRemainSize();

    // 计算缓冲区的剩余空间大小
    size_t RemainSize();

    // 向缓冲区里面写入数据
    void WriteData(char* data);
    
    // 从缓冲区里面读固定长度数据
    void ReadData(char* buffer, size_t len);

    // 清空缓冲区[内存大小不清空]
    void Clear();

    ~Buffer(){ _buffer.resize(0), _read_pos = _write_pos = 0; }
};
#pragma once

/*
    实现缓冲区部分
*/

#include <cstddef>
#include <vector>
#include <cstring>
#include <string>
#include <iostream>

class Buffer{
private:
    inline static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;

private:
    // 写指针移动
    void MoveWritePos(size_t len) { _write_pos += len; }

    // 读指针移动
    void MoveReadPos(size_t len) { _read_pos += len; }

    // 扩容到指定大小
    void ExpandMem(size_t size) { _buffer.resize(size); }

    // 直接拷贝到后置缓冲区
    void DirectCopy(const void *data, size_t len)
    {
        // data方法获取裸指针
        memcpy(_buffer.data() + _write_pos, data, len);

        // 更新写指针位置
        MoveWritePos(len);
    }

    // 先移动，后拷贝
    void MoveCopy(const void *data, size_t len)
    {
        // 提前保存一下
        size_t readable = ReadableSize();

        // 对于同一块空间内的内存数据移动，使用memmove
        // 要不然会有内存重叠问题
        memmove(_buffer.data(), _buffer.data() + _read_pos, readable);

        // 更新读写指针位置
        _read_pos = 0;
        _write_pos = readable;

        memcpy(_buffer.data() + _write_pos, data, len);

        // 更新写指针位置，读指针位置还是0
        MoveWritePos(len);
    }

    // 向缓冲区里面写入数据
    void WriteData(const void *data, size_t len)
    {
        // 拷贝到右边空闲空间就行
        if (len <= AfterRemainSize()) { DirectCopy(data, len); }

        // 将数据拷贝到最前面，再将data数据写到后面
        else if (len <= RemainSize()) { MoveCopy(data, len); }

        // 空闲空间都不够，采用扩容策略
        else{
            // _buffer.resize(size);
            size_t newsize = BufferSize();
            // 避免输入的数据量太大
            while(newsize < ReadableSize() + len){
                newsize *= 2;
            }
            ExpandMem(newsize);
            // TODO，采用先移动，后拷贝的方式
            MoveCopy(data, len);
        }
    }

private:
    std::vector<char> _buffer; // 缓冲区
    size_t _read_pos; // 读指针位置
    size_t _write_pos; // 写指针位置

public:   
    Buffer(const size_t& size = DEFAULT_BUFFER_SIZE)
    :_buffer(size)
    ,_read_pos(0)
    ,_write_pos(0)
    {}

    // 获取缓冲区大小
    size_t BufferSize();

    // 获取读指针当前位置
    size_t GetReadPos();

    // 获取写指针当前位置
    size_t GetWritePos();

    // 可读数据区大小
    size_t ReadableSize();

    // 计算前置剩余空间大小
    size_t FrontRemainSize();

    // 计算后置剩余空间大小
    size_t AfterRemainSize();

    // 计算缓冲区的剩余空间大小
    size_t RemainSize();

    // 从缓冲区里面读固定长度数据
    std::string ReadData(size_t len);

    // 写入C风格的字符串
    void WriteCstrData(const char *data);

    // 写入string类型的字符串
    void WriteStringData(const std::string& data);

    // 清空缓冲区[内存大小不清空]
    void Clear();

    // 获取数据区的头指针位置
    const char* Peek();

    // vector自动析构
    ~Buffer() = default;
};
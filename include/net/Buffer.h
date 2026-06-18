#pragma once

/*
    Buffer模块
    功能：
    1. 缓存socket读写数据
    2. 提供读写指针管理
    3. 支持自动扩容
    4. 支持数据移动复用前置空间
*/

#include <cstddef>
#include <vector>
#include <cstring>
#include <string>
#include <iostream>

#include "base/NoCopy.h"

// 先设置成禁止拷贝，后面再看

class Buffer : public NoCopy {
private:
    inline static constexpr size_t DEFAULT_BUFFER_SIZE = 1024;

private:
    // 写指针移动
    void MoveWritePos(size_t len) { _write_pos += len; }

    // 读指针移动
    void MoveReadPos(size_t len) { _read_pos += len; }

    // 扩容到指定大小
    void ExpandMem(size_t size) { _buffer.resize(size); }

    // 清空缓冲区[内存大小不清空]
    void Clear() { _write_pos = _read_pos = 0; }

    // 计算前置剩余空间大小
    size_t FrontRemainSize() { return GetReadPos(); }

    // 计算后置剩余空间大小
    size_t AfterRemainSize() { return BufferSize() - GetWritePos(); }

    // 获取读指针当前的偏移量
    size_t GetReadPos() { return _read_pos; }

    // 获取写指针当前的偏移量
    size_t GetWritePos() { return _write_pos; }

    // 移动内存使其能够存储下所需数据
    void MoveMem()
    {
        // 提前保存一下
        size_t readable = ReadableSize();

        // 对于同一块空间内的内存数据移动，使用memmove
        // 要不然会有内存重叠问题
        memmove(_buffer.data(), _buffer.data() + _read_pos, readable);

        // 更新读写指针位置
        _read_pos = 0;
        _write_pos = readable;
    }

    // 移动内存+扩容->确保剩余空间能写下len长度的字节
    void EnsureWritable(size_t len)
    {
        if (len <= AfterRemainSize())
            return; 
        if (len <= RemainSize())
        {
            MoveMem();
            return;
        }

        // 实在不够就在这边扩容
        size_t newsize = BufferSize();
        while (newsize < ReadableSize() + len)
        {
            newsize *= 2;
        }
        ExpandMem(newsize);
        // 还是需要挪动内存的
        MoveMem();
        return;
    }

    // 取出定长数据
    std::string GetFixedData(const char* pos, size_t size) const;

private:
    std::vector<char> _buffer; // 缓冲区
    size_t _read_pos; // 读指针的偏移量
    size_t _write_pos; // 写指针的偏移量

public:   
    Buffer(const size_t& size = DEFAULT_BUFFER_SIZE)
    :_buffer(size)
    ,_read_pos(0)
    ,_write_pos(0)
    {}

    // 获取缓冲区大小
    size_t BufferSize();

    // 可读数据区大小
    size_t ReadableSize();

    // 计算缓冲区的剩余空间大小
    size_t RemainSize();

    // 获取当前可读地址
    const char* Peek();

    // 获取当前写入地址
    char* BeginWrite();

    // 消费指定长度的数据，在http请求的时候，可能就是单独取出一部分
    std::string Retrieve(size_t len);

    // 清空所有数据
    void RetrieveAll();

    // 取出缓冲区内的所有数据
    std::string RetrieveAllAsString();

    // 向缓冲区内写入cstr的字符串
    void Append(const void* str, size_t len);

    // 向缓冲区内写入string的字符串
    void Append(const std::string &str);

    // vector自动析构
    ~Buffer() = default;
};
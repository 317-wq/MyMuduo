#include "Buffer.h"

// 获取缓冲区大小
size_t Buffer::BufferSize() { return _buffer.size(); }

// 获取读指针当前位置：对应buffer里面的下标位置
size_t Buffer::GetReadPos() { return _read_pos; }

// 获取写指针当前位置：对应buffer里面的下标位置
size_t Buffer::GetWritePos() { return _write_pos; }

// 可读数据区大小
size_t Buffer::ReadableSize() { return GetWritePos() - GetReadPos(); }

// 计算前置剩余空间大小
size_t Buffer::FrontRemainSize() { return GetReadPos(); }

// 计算后置剩余空间大小
size_t Buffer::AfterRemainSize() { return BufferSize() - GetWritePos(); }

// 计算缓冲区的剩余空间大小
size_t Buffer::RemainSize() { return FrontRemainSize() + AfterRemainSize(); }

// 写入C风格的字符串
void Buffer::WriteCstrData(const char *data) { WriteData(data, strlen(data)); }

// 写入string类型的字符串
void Buffer::WriteStringData(const std::string &data) { WriteData(data.c_str(), data.size()); }

// 读固定长度的数据
std::string Buffer::ReadData(size_t len)
{
    len = std::min(
        len,
        ReadableSize()
    );

    std::string result(
        Peek(),
        len
    );

    _read_pos += len;

    if(_read_pos == _write_pos)
        Clear();

    return result;
}

// 获取数据区的头指针位置
const char *Buffer::Peek() { return _buffer.data() + _read_pos; }

// 清空缓冲区[内存大小不清空]
void Buffer::Clear() { _read_pos = _write_pos = 0; }
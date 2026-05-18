#include "Buffer.h"

// 获取缓冲区大小
size_t Buffer::BufferSize() { return _buffer.size(); }

// 获取读指针当前位置
size_t Buffer::GetReadPos() { return _read_pos; }

// 获取写指针当前位置
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

// 从缓冲区里面读固定长度数据
void Buffer::ReadData(char *buffer, size_t len){

}

// 清空缓冲区[内存大小不清空]
void Buffer::Clear() { _read_pos = _write_pos = 0; }
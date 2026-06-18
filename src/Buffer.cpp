#include "Buffer.h"

// 获取数据区的头指针位置
const char *Buffer::Peek() { return _buffer.data() + _read_pos; }

// 获取当前写入地址
char *Buffer::BeginWrite() { return _buffer.data() + _write_pos; }

// 取出定长数据
std::string Buffer::GetFixedData(const char *pos, size_t size) const{
    return std::string(pos, size);
}

// 消费指定长度的数据，在http请求的时候，可能就是单独取出一部分
std::string Buffer::Retrieve(size_t len) {
    len = (len <= ReadableSize() ? len : ReadableSize());
    // 数据
    std::string str = GetFixedData(Peek(), len);

    // 移动读指针
    MoveReadPos(len);

    // 回到初始位置
    if(_read_pos == _write_pos)
        RetrieveAll();
    
    return str;
}

// 清空所有数据
void Buffer::RetrieveAll() { Clear(); }

// 取出缓冲区内的所有数据
std::string Buffer::RetrieveAllAsString(){
    std::string res = GetFixedData(Peek(), ReadableSize());
    RetrieveAll();
    return res;
}

// 向缓冲区内写入cstr的字符串
void Buffer::Append(const void *str, size_t len){
    // 确保内存空间足够
    EnsureWritable(len);
    // 在writepos后面写入数据
    memcpy(BeginWrite(), str, len);
    // 移动写指针
    MoveWritePos(len);
}

// 向缓冲区内写入string的字符串
void Buffer::Append(const std::string &str){
    Append(str.data(), str.size());
}

// 获取缓冲区大小
size_t Buffer::BufferSize() { return _buffer.size(); }

// 可读数据区大小
size_t Buffer::ReadableSize() { return GetWritePos() - GetReadPos(); }

// 计算缓冲区的剩余空间大小
size_t Buffer::RemainSize() { return FrontRemainSize() + AfterRemainSize(); }

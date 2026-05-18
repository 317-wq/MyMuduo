#include "Buffer.h"

// // 写入C风格的字符串
// void Buffer::WriteCstrData(const char *data) { WriteData(data, strlen(data)); }

// // 写入string类型的字符串
// void Buffer::WriteStringData(const std::string &data) { WriteData(data.c_str(), data.size()); }

// // 读固定长度的数据
// std::string Buffer::ReadData(size_t len)
// {
//     len = std::min(len, ReadableSize());

//     std::string result(Peek(), len);

//     _read_pos += len;

//     if(_read_pos == _write_pos)
//         Clear();

//     return result;
// }

// 获取数据区的头指针位置
const char *Buffer::Peek() { return _buffer.data() + _read_pos; }

// 获取当前写入地址
char *Buffer::BeginWrite() { return _buffer.data() + _write_pos; }

// 消费指定长度的数据，在http请求的时候，可能就是单独取出一部分
void Buffer::Retrieve(size_t len) {
    len = (len <= ReadableSize() ? len : ReadableSize());
    // 移动读指针
    MoveReadPos(len);

    // 回到初始位置
    if(_read_pos == _write_pos)
        RetrieveAll();
}

// 清空所有数据
void Buffer::RetrieveAll() { Clear(); }

// 取出缓冲区内的所有数据
std::string Buffer::RetrieveAllAsString(){
    std::string res(Peek(), ReadableSize());
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
#pragma once

// 由于日志系统是全局共享的，所以这边进行二次修改，使用单例模式-懒汉模式

#include <iostream>
#include <string>
#include <ctime>
#include <mutex>
#include <stdarg.h>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "NoCopy.h"

enum{
    TO_SCREEN = 1,
    TO_FILE = 2
};

enum LogLevel{
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4
};

enum ErrorCode{
    FILE_CREATE_FAIL = 1
};

// 默认路径
// const string PATH = "./log";
// const string FILENAME = "log.txt";

inline constexpr int LOG_SIZE = 1024;
inline const std::string FILEPATH = "../log/log.log";

class Log : public NoCopy{
private:
    int _mode; // 输出到哪里
    std::string _filepath; // 文件路径

    // mutable mutex _mtx;
    std::mutex _mtx; // 这个锁是维护输出缓冲区的

    // 获取日志等级
    std::string GetLogLevel(LogLevel level) const;

    // 获取记录时间
    std::string GetCurrentTime() const;

    // 输出到显示屏
    // 如果后面加了const 就代表成员函数声明为const 所以tx也是const的 无法调用lock unlock方法那些
    // 因此会编译错误 要么删除const 要么给成员变量_mtx加上mutable
    void PrintScreen(const std::string& msg);

    // 输出到文件
    void PrintFile(const std::string& msg);

    void CreateDir();

private:
    Log(const int& mode = TO_FILE,const std::string& filepath = FILEPATH);

public:

    // 使用局部静态变量实现单例
    static Log& GetInstance(const int& mode = TO_FILE,const std::string& filepath = FILEPATH);

    static Log* GetInstancePtr(const int& mode = TO_FILE,const std::string& filepath = FILEPATH);

    // 输出日志信息 可变参数
    void Message(
        const LogLevel& level,
        const char* file,
        int line,
        const char* format,
        ...
    );

public:
    ~Log() = default;
};

#define log Log::GetInstance()

// #define LOG_DEBUG(format,...) \
// log.Message(DEBUG,__FILE__,__LINE__,format,##__VA_ARGS__)

// #define LOG_INFO(format,...) \
// log.Message(INFO,__FILE__,__LINE__,format,##__VA_ARGS__)

// #define LOG_WARNING(format,...) \
// log.Message(WARNING,__FILE__,__LINE__,format,##__VA_ARGS__)

// #define LOG_ERROR(format,...) \
// log.Message(ERROR,__FILE__,__LINE__,format,##__VA_ARGS__)

// #define LOG_FATAL(format,...) \
// log.Message(FATAL,__FILE__,__LINE__,format,##__VA_ARGS__)

#define LOG_DEBUG(format,...) log.Message(DEBUG,__FILE__,__LINE__,format,##__VA_ARGS__)
#define LOG_INFO(format,...) log.Message(INFO,__FILE__,__LINE__,format,##__VA_ARGS__)
#define LOG_WARNING(format,...) log.Message(WARNING,__FILE__,__LINE__,format,##__VA_ARGS__)
#define LOG_ERROR(format,...) log.Message(ERROR,__FILE__,__LINE__,format,##__VA_ARGS__)
#define LOG_FATAL(format,...) log.Message(FATAL,__FILE__,__LINE__,format,##__VA_ARGS__)
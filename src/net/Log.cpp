#include "net/Log.h"

#include <system_error>

// 获取日志等级
std::string Log::GetLogLevel(LogLevel level) const{
    switch(level){
        case DEBUG:return "DEBUG";
        case INFO:return "INFO";
        case WARNING:return "WARNING";
        case ERROR:return "ERROR";
        case FATAL:return "FATAL";
        default:return "UNKNOWN";
    }
}

// 获取记录时间
std::string Log::GetCurrentTime() const{
    time_t now = time(0);

    // 获取1970.0.0到现在秒数

    tm local_time;

    // POSIX标准，线程安全

    localtime_r(&now,&local_time);

    char buffer[80];

    strftime(buffer,sizeof buffer,"%Y-%m-%d %H:%M:%S",&local_time);

    return std::string(buffer);
}

// 输出到显示屏
void Log::PrintScreen(const std::string& msg){
    std::unique_lock<std::mutex> lock(_mtx);

    std::cout << msg << std::endl;
}

// 输出到文件
void Log::PrintFile(const std::string& msg){
    bool written = false;
    {
        std::unique_lock<std::mutex> lock(_mtx);

        std::ofstream file(_filepath,std::ios::app);

        if(file.is_open()){
            file << msg << std::endl;
            written = true;
        }
    }

    if(!written){
        std::cerr << "ERROR: Failed to open log file: " << _filepath << std::endl;

        // 先释放日志锁，再回退到屏幕输出，避免递归加锁造成死锁。
        PrintScreen(msg);
    }
}

void Log::CreateDir(){
    const std::filesystem::path filepath(_filepath);
    const std::filesystem::path parent = filepath.parent_path();

    if(!parent.empty()){
        std::error_code error;
        std::filesystem::create_directories(parent,error);
        if(error){
            std::cerr << "ERROR: Failed to create log directory: "
                      << parent << ": " << error.message() << std::endl;
            return;
        }
    }

    // 以追加方式打开可在日志文件不存在时创建文件。
    std::ofstream file(filepath,std::ios::app);
    if(!file.is_open()){
        std::cerr << "ERROR: Failed to create log file: " << _filepath << std::endl;
    }
}

Log::Log(const int& mode,const std::string& filepath)
:_mode(mode)
,_filepath(filepath)
{
    CreateDir();
}

// 使用局部静态变量实现单例
Log& Log::GetInstance(const int& mode,const std::string& filepath){
    static Log instance(mode,filepath);

    return instance;
}

Log* Log::GetInstancePtr(const int& mode,const std::string& filepath){
    return &GetInstance(mode,filepath);
}

// 输出日志信息 可变参数
void Log::Message(
    const LogLevel& level,
    const char* file,
    int line,
    const char* format,
    ...
){

    std::string mtime = GetCurrentTime();

    std::string mlevel = GetLogLevel(level);

    char buffer[LOG_SIZE];

    va_list args;

    // 指向format的指针
    va_start(args,format);

    // 初始化va_list
    // 指向第一个可变参数
    vsnprintf(buffer,sizeof buffer,format,args);

    va_end(args);

    // 结束访问
    std::string msg =
        "[" + mlevel +
        "][" + mtime +
        "][" + std::string(file) +
        ":" + std::to_string(line) +
        "] " +
        std::string(buffer);

    // 看_mode是输出到哪里
    if(_mode & TO_SCREEN)
        PrintScreen(msg);

    if(_mode & TO_FILE)
        PrintFile(msg);
}

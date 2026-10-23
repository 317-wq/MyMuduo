#include "HttpUtils.h"
#include "net/Log.h"
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>

std::string FindStaticDir() {
    static const char* kPaths[] = {"./static", "../static"};
    for (const char* path : kPaths) {
        std::ifstream f(std::string(path) + "/html/login.html");
        if (f.good()) return path;
    }
    return "";
}

void RunDBSync(Database* db, std::function<void(sql::Connection*)> fn) {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    bool fn_called = false;
    // nullptr就是在本线程里面去跑
    db->Execute(nullptr, [&](sql::Connection* conn) {
        fn_called = true;
        fn(conn);
    }, [&]() {
        std::lock_guard<std::mutex> lk(mtx);
        done = true;
        cv.notify_one();
    });
    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&] { return done; });
    if (!fn_called) LOG_ERROR("RunDBSync: DB connection pool exhausted after retries, task dropped");
}

bool ParseJsonBody(const httplib::Request& req, Json::Value& root) {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream iss(req.body);
    return Json::parseFromStream(builder, iss, &root, &errs);
}

void SendJson(httplib::Response& res, const Json::Value& root) {
    Json::StreamWriterBuilder builder;
    builder["emitUTF8"] = true;
    builder["indentation"] = "";
    res.set_content(Json::writeString(builder, root), "application/json; charset=utf-8");
}

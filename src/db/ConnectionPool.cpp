#include "db/ConnectionPool.h"

#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/exception.h>
#include <cppconn/statement.h>

#include <chrono>

ConnectionPool::ConnectionPool(const std::string& host, int port,
                               const std::string& user, const std::string& pass,
                               const std::string& dbname, int max_size)
    : _host(host), _port(port), _user(user), _pass(pass), _dbname(dbname)
    , _max_size(max_size)
{
}

ConnectionPool::~ConnectionPool()
{
    _closed = true;
    _cv.notify_all();

    std::lock_guard<std::mutex> lock(_mutex);
    while (!_pool.empty()) {
        sql::Connection* conn = _pool.front();
        _pool.pop();
        delete conn;
        _active_count--;
    }
}

sql::Connection* ConnectionPool::CreateConnection()
{
    sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();

    std::string url = "tcp://" + _host + ":" + std::to_string(_port);
    sql::Connection* conn = driver->connect(url, _user, _pass);
    conn->setSchema(_dbname);

    return conn;
}

bool ConnectionPool::CheckHealth(sql::Connection* conn)
{
    if (!conn) return false;

    try {
        // isClosed() 检查 TCP 连接是否断开
        if (conn->isClosed()) return false;

        // 尝试轻量级 ping，确认 MySQL 服务端仍可通信
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery("SELECT 1"));
        return rs->next();
    } catch (const sql::SQLException&) {
        return false;
    }
}

sql::Connection* ConnectionPool::Borrow(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(_mutex);

    while (!_closed) {
        // 1. 先从空闲队列取一个连接
        if (!_pool.empty()) {
            sql::Connection* conn = _pool.front();
            _pool.pop();

            // 检查健康状态
            if (CheckHealth(conn)) {
                return conn;  // 健康连接，直接返回
            }

            // 连接已失效，销毁并继续尝试
            delete conn;
            _active_count--;
            continue;
        }

        // 2. 空闲队列为空，但还可以创建新连接
        if (_active_count < _max_size) {
            // 在锁外创建连接比较耗时，但为了简单这里在锁内创建
            try {
                sql::Connection* conn = CreateConnection();
                _active_count++;
                return conn;
            } catch (const sql::SQLException& e) {
                // 创建失败（MySQL 可能挂了），不增加 active_count
                // 返回 nullptr，让调用者稍后重试
                return nullptr;
            }
        }

        // 3. 连接数已达上限，等待归还
        if (timeout_ms > 0) {
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(timeout_ms);
            if (_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                return nullptr;  // 超时
            }
        } else {
            _cv.wait(lock);  // 无限等待
        }
    }

    return nullptr;  // 池已关闭
}

void ConnectionPool::Return(sql::Connection* conn)
{
    if (!conn) return;

    std::lock_guard<std::mutex> lock(_mutex);

    // 简单检查：连接已关闭则丢弃
    if (conn->isClosed()) {
        delete conn;
        _active_count--;
    } else {
        _pool.push(conn);
    }

    _cv.notify_one();
}

void ConnectionPool::Remove(sql::Connection* conn)
{
    if (!conn) return;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active_count--;
    }

    delete conn;
    _cv.notify_one();
}

void ConnectionPool::Close()
{
    _closed = true;
    _cv.notify_all();
}

int ConnectionPool::IdleCount()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return static_cast<int>(_pool.size());
}

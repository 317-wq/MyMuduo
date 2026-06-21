/*
    数据库 + 用户服务集成测试

    测试流程：
    1. 发送验证码 → 注册 → 登录
    2. 重复注册（应失败）
    3. 错误密码登录（应失败）
*/

#include "db/Database.h"
#include "db/UserDao.h"
#include "service/UserService.h"
#include "base/Crypto.h"
#include "net/EventLoop.h"

#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>

// 测试配置（与 config.ini 保持一致）
const std::string DB_HOST = "127.0.0.1";
const int DB_PORT = 3306;
const std::string DB_USER = "root";
const std::string DB_PASS = "lijiatong344A@";
const std::string DB_NAME = "mymuduo";

// 辅助：等待异步操作完成
static void WaitForDB(EventLoop& loop, int max_retries = 50)
{
    for (int i = 0; i < max_retries; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        loop.DoPendingFunctors();
        // DoPendingFunctors 如果处理了回调，回调可能又产生了新的异步操作...
        // 这里简化处理，用重试
    }
}

int main()
{
    std::cout << "=== 数据库 + 用户服务集成测试 ===" << std::endl;

    // 1. 初始化数据库连接池
    Database db(DB_HOST, DB_PORT, DB_USER, DB_PASS, DB_NAME, 2);
    std::cout << "[OK] Database connection pool created" << std::endl;

    // 2. 创建 EventLoop（主线程）
    EventLoop loop;
    UserService service(&db, &loop);

    std::cout << "[OK] UserService created" << std::endl;

    // 测试数据
    const std::string test_email = "test_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        ) + "@example.com";
    const std::string test_password = "MyPass123";
    const std::string test_username = "TestUser";

    std::cout << "Test email: " << test_email << std::endl;
    std::cout << "========================================" << std::endl;

    // ---------- 测试 1: 正常注册流程 ----------
    std::cout << "\n[Test 1] 正常注册流程" << std::endl;

    // 手动插入验证码（模拟用户收到邮件后填入）
    const std::string verification_code = "123456";
    db.Execute(&loop,
        [&](sql::Connection* conn) {
            UserDao::InsertVerificationCode(conn, test_email,
                                            verification_code, 1, 300);
        },
        []() {});
    WaitForDB(loop);
    std::cout << "  Verification code inserted: " << verification_code << std::endl;

    // 用验证码完成注册
    uint32_t registered_user_id = 0;
    bool register_done = false;

    service.RegisterWithCode(test_email, verification_code,
                             test_password, test_username,
        [&](int err, const std::string& msg, uint32_t user_id) {
            std::cout << "  Register: err=" << err << " msg=" << msg
                      << " user_id=" << user_id << std::endl;
            assert(err == 0);
            assert(user_id > 0);
            registered_user_id = user_id;
            register_done = true;
        });

    WaitForDB(loop);
    assert(register_done);
    std::cout << "[PASS] Test 1: 注册成功, user_id=" << registered_user_id << std::endl;

    // ---------- 测试 2: 重复注册应失败 ----------
    std::cout << "\n[Test 2] 重复注册应失败" << std::endl;

    // 先插入新验证码
    db.Execute(&loop,
        [&](sql::Connection* conn) {
            UserDao::InsertVerificationCode(conn, test_email, "654321", 1, 300);
        },
        []() {});
    WaitForDB(loop);

    int dup_err = -1;
    service.RegisterWithCode(test_email, "654321", test_password, "AnotherUser",
        [&](int err, const std::string& msg, uint32_t) {
            std::cout << "  Register again: err=" << err << " msg=" << msg << std::endl;
            dup_err = err;
        });

    WaitForDB(loop);
    assert(dup_err == 2);  // 该邮箱已被注册
    std::cout << "[PASS] Test 2: 重复注册被正确拒绝" << std::endl;

    // ---------- 测试 3: 正常登录 ----------
    std::cout << "\n[Test 3] 正常登录" << std::endl;

    bool login_done = false;
    UserInfo logged_in_user;

    service.Login(test_email, test_password,
        [&](int err, const std::string& msg, const UserInfo& user) {
            std::cout << "  Login: err=" << err << " msg=" << msg << std::endl;
            assert(err == 0);
            std::cout << "  User: id=" << user.id
                      << " email=" << user.email
                      << " username=" << user.username
                      << " created_at=" << user.created_at << std::endl;
            assert(user.id == registered_user_id);
            assert(user.email == test_email);
            assert(user.username == test_username);
            logged_in_user = user;
            login_done = true;
        });

    WaitForDB(loop);
    assert(login_done);
    std::cout << "[PASS] Test 3: 登录成功" << std::endl;

    // ---------- 测试 4: 错误密码登录应失败 ----------
    std::cout << "\n[Test 4] 错误密码登录应失败" << std::endl;

    int wrong_pass_err = -1;
    service.Login(test_email, "WrongPassword",
        [&](int err, const std::string& msg, const UserInfo&) {
            std::cout << "  Login with wrong password: err=" << err
                      << " msg=" << msg << std::endl;
            wrong_pass_err = err;
        });

    WaitForDB(loop);
    assert(wrong_pass_err == 2);  // 密码错误
    std::cout << "[PASS] Test 4: 错误密码被正确拒绝" << std::endl;

    // ---------- 测试 5: 不存在的用户登录 ----------
    std::cout << "\n[Test 5] 不存在的用户登录应失败" << std::endl;

    int nonexist_err = -1;
    service.Login("nonexistent@example.com", "whatever",
        [&](int err, const std::string& msg, const UserInfo&) {
            std::cout << "  Login nonexistent: err=" << err
                      << " msg=" << msg << std::endl;
            nonexist_err = err;
        });

    WaitForDB(loop);
    assert(nonexist_err == 1);  // 该邮箱未注册
    std::cout << "[PASS] Test 5: 不存在用户被正确拒绝" << std::endl;

    std::cout << "\n========================================" << std::endl;
    std::cout << "=== 所有测试通过！===" << std::endl;

    return 0;
}

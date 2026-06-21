#pragma once

/*
    加密算法
    - SHA-256(salt + password) 哈希
    - 随机盐值生成
    - 邮箱验证码生成
*/

#include <string>

class Crypto {
public:
    // 生成随机盐值（16 字节 → 32 字符 hex）
    static std::string GenerateSalt();

    // SHA-256(salt + password)，返回 64 字符 hex 字符串
    static std::string HashPassword(const std::string& password,
                                    const std::string& salt);

    // 验证密码：HashPassword(password, salt) == hash
    static bool VerifyPassword(const std::string& password,
                               const std::string& salt,
                               const std::string& hash);

    // 生成 6 位数字验证码
    static std::string GenerateVerificationCode();
};

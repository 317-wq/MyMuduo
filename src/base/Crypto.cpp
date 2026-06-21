#include "base/Crypto.h"

#include <openssl/sha.h>
#include <openssl/rand.h>

#include <random>
#include <sstream>
#include <iomanip>

// 将字节数组转为 hex 字符串
static std::string ToHex(const unsigned char* data, size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

// 随机盐值
std::string Crypto::GenerateSalt()
{
    unsigned char buf[16];
    if (RAND_bytes(buf, sizeof(buf)) != 1) {
        // 如果 OpenSSL 随机数失败，回退到 std::random_device
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(0, 255);
        for (int i = 0; i < 16; ++i) {
            buf[i] = static_cast<unsigned char>(dist(gen));
        }
    }
    return ToHex(buf, 16);
}

std::string Crypto::HashPassword(const std::string& password,
                                 const std::string& salt)
{
    std::string data = salt + password;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);

    return ToHex(hash, SHA256_DIGEST_LENGTH);
}

bool Crypto::VerifyPassword(const std::string& password,
                            const std::string& salt,
                            const std::string& hash)
{
    return HashPassword(password, salt) == hash;
}

// 六位验证码
std::string Crypto::GenerateVerificationCode()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(100000, 999999);
    return std::to_string(dist(gen));
}

#include "service/EmailSender.h"

#include <curl/curl.h>

#include <sstream>
#include <cstring>

// ============================================================
// libcurl SMTP 实现
// ============================================================

// 回调：libcurl 通过此函数读取邮件内容
struct ReadContext {
    const std::string& data;
    size_t pos = 0;
};

static size_t ReadCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* ctx = static_cast<ReadContext*>(userdata);
    size_t buffer_size = size * nmemb;
    size_t remaining = ctx->data.size() - ctx->pos;
    size_t to_copy = (remaining < buffer_size) ? remaining : buffer_size;

    if (to_copy > 0) {
        memcpy(ptr, ctx->data.data() + ctx->pos, to_copy);
        ctx->pos += to_copy;
        return to_copy;
    }
    return 0;  // 发送完毕
}

static std::string BuildMimeMessage(const std::string& from,
                                    const std::string& from_name,
                                    const std::string& to,
                                    const std::string& subject,
                                    const std::string& body)
{
    std::ostringstream msg;
    msg << "From: " << from_name << " <" << from << ">\r\n";
    msg << "To: <" << to << ">\r\n";
    msg << "Subject: =?UTF-8?B?";

    // Base64 编码 subject（内联）
    static const char kBase64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* data = reinterpret_cast<const unsigned char*>(subject.data());
    size_t len = subject.size();
    std::string b64;
    int val = 0, valb = -6;
    for (size_t i = 0; i < len; ++i) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            b64.push_back(kBase64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        b64.push_back(kBase64[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (b64.size() % 4) b64.push_back('=');
    msg << b64;

    msg << "?=\r\n";
    msg << "Content-Type: text/plain; charset=UTF-8\r\n";
    msg << "Content-Transfer-Encoding: 8bit\r\n";
    msg << "\r\n";
    msg << body;

    return msg.str();
}

bool EmailSender::Send(const Config& cfg,
                       const std::string& to,
                       const std::string& subject,
                       const std::string& body)
{
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    // 收件人列表
    struct curl_slist* recipients = nullptr;
    recipients = curl_slist_append(recipients, to.c_str());

    std::string url = "smtp://" + cfg.smtp_host + ":" + std::to_string(cfg.smtp_port);

    std::string from_email = cfg.smtp_user.empty()
        ? "noreply@mymuduo.local" : cfg.smtp_user;

    std::string mime_msg = BuildMimeMessage(from_email, cfg.from_name,
                                            to, subject, body);

    ReadContext ctx{mime_msg, 0};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from_email.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    // 认证（如果配置了用户名密码）
    if (!cfg.smtp_user.empty() && !cfg.smtp_pass.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, cfg.smtp_user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg.smtp_pass.c_str());
    }

    // TLS：587 端口使用 STARTTLS，465 端口使用隐式 SSL
    if (cfg.smtp_port == 465) {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    } else {
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
    }

    // 超时
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

bool EmailSender::SendVerificationCode(const Config& cfg,
                                       const std::string& to,
                                       const std::string& code,
                                       int expire_minutes)
{
    std::string subject = cfg.from_name + " 邮箱验证码";

    std::ostringstream body;
    body << "您好！\n\n"
         << "您的验证码是：" << code << "\n\n"
         << "验证码有效期为 " << expire_minutes << " 分钟，请尽快使用。\n"
         << "如果这不是您的操作，请忽略此邮件。\n\n"
         << cfg.from_name << " 团队\n";

    return Send(cfg, to, subject, body.str());
}

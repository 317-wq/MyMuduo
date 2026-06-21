#pragma once

/*
    简单 SMTP 邮件发送器

    使用原始 POSIX socket 实现 SMTP 协议（EHLO → AUTH LOGIN → MAIL FROM →
    RCPT TO → DATA → QUIT）。

    仅支持 AUTH LOGIN 认证，适用于：
    - 本地 MTA（postfix/sendmail）监听 localhost:25
    - 第三方 SMTP 服务（需要支持非 TLS 连接或通过本地中继）

    典型用法：
        EmailSender::Config cfg;
        cfg.smtp_host = "smtp.example.com";
        cfg.smtp_port = 587;
        cfg.smtp_user = "noreply@example.com";
        cfg.smtp_pass = "xxx";

        EmailSender::Send(cfg, "user@example.com",
                          "MyMuduo 验证码",
                          "您的验证码是：123456");
*/

#include <string>

class EmailSender {
public:
    struct Config {
        std::string smtp_host;
        int smtp_port = 587;
        std::string smtp_user;
        std::string smtp_pass;
        std::string from_name = "MyMuduo";
    };

    // 发送一封邮件，成功返回 true
    static bool Send(const Config& cfg,
                     const std::string& to,
                     const std::string& subject,
                     const std::string& body);

    // 便捷方法：发送验证码邮件
    static bool SendVerificationCode(const Config& cfg,
                                     const std::string& to,
                                     const std::string& code,
                                     int expire_minutes = 5);
};

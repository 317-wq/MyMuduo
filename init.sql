-- MyMuduo 数据库初始化脚本
-- mysql -u root -p < init.sql

CREATE DATABASE IF NOT EXISTS mymuduo
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE mymuduo;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    email       VARCHAR(255) NOT NULL UNIQUE,
    password    VARCHAR(255) NOT NULL COMMENT 'SHA-256(salt + password) 的 hex 结果',
    salt        VARCHAR(64) NOT NULL COMMENT '随机盐值，hex 编码',
    username    VARCHAR(64) NOT NULL,
    avatar      VARCHAR(512) DEFAULT '/img/default.svg' COMMENT '头像文件路径',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_email (email),
    INDEX idx_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 邮箱验证码表
-- type: 1=注册验证, 2=重置密码
CREATE TABLE IF NOT EXISTS verification_codes (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    email       VARCHAR(255) NOT NULL,
    code        VARCHAR(8) NOT NULL,
    type        TINYINT NOT NULL DEFAULT 1 COMMENT '1=注册, 2=重置密码',
    expires_at  TIMESTAMP NOT NULL COMMENT '过期时间',
    used        TINYINT NOT NULL DEFAULT 0 COMMENT '0=未使用, 1=已使用',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_email_code (email, code),
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

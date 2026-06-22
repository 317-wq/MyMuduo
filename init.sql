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
    avatar          VARCHAR(512) DEFAULT '/img/default.svg' COMMENT '头像文件路径',
    gender          TINYINT NOT NULL DEFAULT 0 COMMENT '0=未设置, 1=男, 2=女',
    birthday        DATE DEFAULT NULL,
    secondary_email VARCHAR(255) DEFAULT '' COMMENT '备用邮箱',
    created_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at      TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
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

-- 好友关系表
-- status: 0=pending(等待确认), 1=accepted(已接受), 2=blocked(已拉黑)
-- 双向好友在 accepted 后会插入两条记录 (A→B 和 B→A)
CREATE TABLE IF NOT EXISTS friends (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    user_id     INT UNSIGNED NOT NULL,
    friend_id   INT UNSIGNED NOT NULL,
    status      TINYINT NOT NULL DEFAULT 0 COMMENT '0=pending, 1=accepted, 2=blocked',
    remark      VARCHAR(64) DEFAULT '' COMMENT '好友备注名',
    created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_user_friend (user_id, friend_id),
    INDEX idx_user_status (user_id, status),
    INDEX idx_friend_status (friend_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 私聊消息表
CREATE TABLE IF NOT EXISTS private_messages (
    id           INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    from_user_id INT UNSIGNED NOT NULL,
    to_user_id   INT UNSIGNED NOT NULL,
    content      TEXT NOT NULL,
    is_read      TINYINT NOT NULL DEFAULT 0 COMMENT '0=未读, 1=已读',
    created_at   TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_conversation (from_user_id, to_user_id, id),
    INDEX idx_unread (to_user_id, is_read, id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================================
-- 升级已有数据库（如果列已存在会报错，可忽略）
-- ============================================================
-- ALTER TABLE users ADD COLUMN gender TINYINT NOT NULL DEFAULT 0 COMMENT '0=未设置, 1=男, 2=女';
-- ALTER TABLE users ADD COLUMN birthday DATE DEFAULT NULL;
-- ALTER TABLE users ADD COLUMN secondary_email VARCHAR(255) DEFAULT '' COMMENT '备用邮箱';
-- ALTER TABLE friends ADD COLUMN remark VARCHAR(64) DEFAULT '' COMMENT '好友备注名';

-- 消息增强功能
-- ALTER TABLE private_messages ADD COLUMN is_revoked TINYINT NOT NULL DEFAULT 0 COMMENT '0=正常, 1=已撤回';
-- ALTER TABLE private_messages ADD COLUMN reply_to_id INT UNSIGNED DEFAULT NULL COMMENT '回复的消息ID';
-- ALTER TABLE private_messages ADD COLUMN reply_preview VARCHAR(200) DEFAULT '' COMMENT '被回复消息的摘要';
-- ALTER TABLE private_messages ADD COLUMN updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP;

/**
 * MyMuduo Chat — 前端交互脚本
 *
 * TODO: 连接 WebSocket / 自定义 TCP 协议
 * 当前为静态占位，后续与后端协议对接
 */

(function() {
    'use strict';

    console.log('MyMuduo Chat client loaded');

    // ============================================================
    // 标签页切换
    // ============================================================
    const tabs = document.querySelectorAll('.tab');
    const loginForm = document.getElementById('login-form');
    const registerForm = document.getElementById('register-form');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            const target = tab.dataset.tab;
            if (target === 'login') {
                loginForm.style.display = 'flex';
                registerForm.style.display = 'none';
            } else {
                loginForm.style.display = 'none';
                registerForm.style.display = 'flex';
            }
        });
    });

    // ============================================================
    // 登录表单提交
    // ============================================================
    loginForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const email = document.getElementById('login-email').value;
        const password = document.getElementById('login-password').value;
        console.log('Login:', email, password);
        // TODO: 通过 WebSocket 发送 LoginRequest
    });

    // ============================================================
    // 发送验证码
    // ============================================================
    document.getElementById('btn-send-code').addEventListener('click', () => {
        const email = document.getElementById('reg-email').value;
        if (!email) {
            showMessage('请先输入邮箱', 'error');
            return;
        }
        console.log('Send verification code to:', email);
        showMessage('验证码已发送（请查看服务端日志）', 'info');
        // TODO: 通过 WebSocket 发送 SendCodeRequest
    });

    // ============================================================
    // 注册表单提交
    // ============================================================
    registerForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const email = document.getElementById('reg-email').value;
        const code = document.getElementById('reg-code').value;
        const username = document.getElementById('reg-username').value;
        const password = document.getElementById('reg-password').value;
        console.log('Register:', email, username, code);
        // TODO: 通过 WebSocket 发送 RegisterRequest
    });

    // ============================================================
    // 工具函数
    // ============================================================
    function showMessage(msg, type) {
        const el = document.getElementById('auth-message');
        el.textContent = msg;
        el.style.color = type === 'error' ? '#ff6b6b' : '#6bff6b';
    }

})();

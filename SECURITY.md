# 安全策略

## 报告漏洞

请不要在公开的 Issue 里报告安全问题。通过 GitHub 的 [私密漏洞报告](https://github.com/VOfficial404/obs-bililive/security/advisories/new) 提交，说明影响范围和复现步骤。我们会尽快回复。

## 支持的版本

只有最新发布的版本会收到安全修复。

## 凭据

插件保存的 B 站登录凭据（`SESSDATA`、`bili_jct`、`DedeUserID`、`buvid3`）能直接登录你的 B 站账号，各平台的保存方式不同：

- **Windows**：用系统的 DPAPI 加密后写进 OBS 配置目录下的 `plugin_config/obs-bililive/config.json`，只有同一台电脑上的同一个 Windows 用户能解密。
- **macOS**：存在「钥匙串」里（服务名 `obs-bililive`），只在本机解锁后可用，不会同步到 iCloud；`config.json` 里不含凭据。
- **Linux**：以明文写进 `config.json`，文件权限为 0600，只有你自己的账户能读。能读取这个文件的人（包括 root 和以你的身份运行的程序）就能登录你的账号。

在插件里退出登录会让这份会话在 B 站失效，并清除本地保存的凭据（包括 `config.json.bak` 备份）。

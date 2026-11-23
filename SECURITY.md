# 安全策略

## 报告漏洞

请不要在公开的 Issue 里报告安全问题。通过 GitHub 的 [私密漏洞报告](https://github.com/VOfficial404/obs-bililive/security/advisories/new) 提交，说明影响范围和复现步骤。我们会尽快回复。

## 支持的版本

只有最新发布的版本会收到安全修复。

## 凭据

插件把 B 站登录凭据（`SESSDATA`、`bili_jct`、`DedeUserID`、`buvid3`）以明文保存在 OBS 配置目录下的 `plugin_config/obs-bililive/config.json`，权限与 OBS 的其他配置文件相同。能读取这个文件的人就能登录你的 B 站账号。在插件里退出登录会让这份会话在 B 站失效，并清除文件里的凭据。

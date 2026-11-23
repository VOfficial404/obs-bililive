# 更新日志

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

## [0.2.0]

### 新增

- 弹幕悬浮窗：始终置顶、可拖动和调整大小，锁定后鼠标穿透；可调背景不透明度和字号；顶部显示在线和看过人数。
- 悬浮窗在 Windows 10 2004 及以上默认不会被显示器采集和窗口采集拍到。
- 两个 OBS 快捷键：显示/隐藏弹幕悬浮窗、锁定/解锁弹幕悬浮窗。
- [安装指南](docs/INSTALL.md)：官网版、Steam 版、便携版、macOS 和 Linux 的安装步骤与排查方法。

### 变更

- Windows 安装包同时包含 OBS 33 的新插件结构和 OBS 31、32 的旧结构，OBS 33 不会再把插件标成旧版。

## [0.1.0]

首个版本。

### 新增

- B 站直播停靠窗口：扫码登录、退出登录，未开通直播间时可一键开通。
- 以直播姬 PC 版的身份开播和关播，推流地址自动写入 OBS 并开始推流；OBS 停止推流时自动关播。
- 开播前修改标题、选择分区，记住上次的分区。
- 分区要求人脸认证（60024）时弹出二维码，认证完成后自动继续开播。
- 实时弹幕面板：弹幕、礼物、醒目留言、上舰、关注、分享、进入直播间。
- 看过、在线、点赞、粉丝数统计。
- 在面板里发送弹幕。
- 简体中文和英文界面。

[Unreleased]: https://github.com/VOfficial404/obs-bililive/compare/0.2.0...HEAD
[0.2.0]: https://github.com/VOfficial404/obs-bililive/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/VOfficial404/obs-bililive/releases/tag/0.1.0

# 更新日志

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 新增

- 弹幕显示身份标签：主播、房管、舰长、提督、总督；主播和大航海成员的名字高亮。
- 粉丝牌使用 B 站下发的真实颜色，熄灭的粉丝牌显示为灰色。

## [0.4.0]

### 修复

- 扫码登录在手机上确认后提示「QR login succeeded without a session」、无法登录：B 站改为只通过 `Set-Cookie` 下发登录凭据，插件现在从响应头读取。

### 新增

- Windows 安装程序 `obs-bililive-*-windows-x64-setup.exe`：默认装到 OBS 插件目录，再次运行即升级，OBS 开着时提示关闭，可在「已安装的应用」里卸载。
- 启动时检查 GitHub 上的新版本，有新版本时在「B站直播」菜单和弹幕区提示。

## [0.3.1]

### 新增

- OBS 顶栏新增「B站直播」菜单，位于「工具」和「帮助」之间：显示面板、开始/结束直播，以及原 ⋯ 菜单里的全部选项。

## [0.3.0]

### 变更

- 面板改成直播姬风格：深色卡片、粉色开播按钮、四格直播数据（大数字按「万」显示）、「直播中」标签。
- 弹幕列表分为「弹幕 / 礼物 / 醒目留言」三个标签页；粉丝牌按等级上色，醒目留言按金额显示为彩色卡片，上舰高亮。悬浮窗使用同样的样式。

### 新增

- 面板显示房间号，可一键复制；菜单里可复制房间号和直播间链接，方便同时使用 blivechat 等其他弹幕姬。

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

[Unreleased]: https://github.com/VOfficial404/obs-bililive/compare/0.4.0...HEAD
[0.4.0]: https://github.com/VOfficial404/obs-bililive/compare/0.3.1...0.4.0
[0.3.1]: https://github.com/VOfficial404/obs-bililive/compare/0.3.0...0.3.1
[0.3.0]: https://github.com/VOfficial404/obs-bililive/compare/0.2.0...0.3.0
[0.2.0]: https://github.com/VOfficial404/obs-bililive/compare/0.1.0...0.2.0
[0.1.0]: https://github.com/VOfficial404/obs-bililive/releases/tag/0.1.0

<div align="center">

# BiliLive for OBS

**不开直播姬，直接用 OBS 在 B 站开播。**

一个 OBS Studio 停靠窗口插件：扫码登录、改标题、选分区、一键开播和关播，<br>
弹幕、礼物、醒目留言、上舰都在 OBS 里看，也能直接发弹幕。

[![版本](https://img.shields.io/github/v/release/VOfficial404/obs-bililive?color=fb7299&label=版本)](https://github.com/VOfficial404/obs-bililive/releases/latest)
[![构建](https://img.shields.io/github/actions/workflow/status/VOfficial404/obs-bililive/push.yaml?label=构建)](https://github.com/VOfficial404/obs-bililive/actions)
[![许可](https://img.shields.io/github/license/VOfficial404/obs-bililive?label=许可)](LICENSE)

[下载](https://github.com/VOfficial404/obs-bililive/releases/latest) · [安装](#安装) · [使用](#使用) · [常见问题](#常见问题) · [English](#english)

</div>

## 为什么需要它

B 站规定粉丝数不足 5000 的主播只能用「哔哩哔哩直播姬」开播，网页上拿不到推流码，OBS 也就用不了。

这个插件在 OBS 里按直播姬 PC 版的方式向 B 站申请开播，拿到推流地址后直接写进 OBS 的推流设置，然后开始推流。画面、音频、编码器还是你熟悉的 OBS，不用再开第二个软件。

## 功能

- **扫码登录**：用哔哩哔哩 App 扫一下就登录，不用复制 Cookie。
- **一键开播 / 关播**：开播时自动把推流地址和推流码填进 OBS，并开始推流；在 OBS 里停止推流时自动关播（两项都能在菜单里关掉）。
- **标题和分区**：开播前改标题、选分区，下次打开记住上次的分区。
- **人脸认证**：分区要求人脸认证时弹出二维码，用 App 扫码认证后自动继续开播。
- **弹幕面板**：实时显示弹幕（带粉丝牌）、礼物、醒目留言、上舰、关注、分享和进入直播间。
- **直播数据**：看过人数、在线人数、点赞数、粉丝数。
- **发送弹幕**：在面板底部直接发弹幕。
- **没有直播间？** 插件里就能开通。
- 中文和英文界面跟随 OBS 的语言设置。

## 安装

需要 OBS Studio 31 或更新版本。到 [Releases](https://github.com/VOfficial404/obs-bililive/releases/latest) 下载对应系统的安装包：

| 系统 | 文件 | 安装方式 |
|---|---|---|
| Windows | `obs-bililive-*-windows-x64.zip` | 解压后把 `obs-bililive` 文件夹放到 `C:\ProgramData\obs-studio\plugins\` |
| macOS | `obs-bililive-*-macos-universal.pkg` | 双击安装 |
| Ubuntu | `obs-bililive-*-x86_64-linux-gnu.deb` | `sudo apt install ./obs-bililive-*.deb` |

装好后重启 OBS，在菜单 **停靠窗口 → B站直播** 打开面板。

## 使用

1. 点 **扫码登录**，用哔哩哔哩 App 扫码并确认。
2. 填好标题，选好分区。
3. 点 **开始直播**。插件向 B 站申请开播，把推流地址写进 OBS，然后开始推流。
4. 播完点 **结束直播**，或者直接在 OBS 里点「停止直播」。

面板右上角的 **⋯** 菜单里可以：

- 关掉「开播后自动开始推流」，改为手动点 OBS 的「开始直播」；
- 关掉「OBS 停止推流时自动关播」，断流重推时直播间不会下播；
- 隐藏进场和礼物消息；
- 打开直播间、刷新、退出登录。

> 每次开播 B 站都会生成新的推流码，所以请用面板上的「开始直播」开播，而不是直接点 OBS 的「开始直播」。

## 常见问题

**提示需要人脸认证（60024）**
部分分区开播前要人脸认证。插件会弹出二维码，用哔哩哔哩 App 扫码完成认证后会自动继续开播。

**提示额外的身份验证（60043）**
这是另一种风控验证，插件还不支持。在直播姬里开播一次完成验证，之后就能继续用插件开播。

**推流失败或画面卡住**
B 站并没有给所有主播开放 HEVC（H.265）推流。请在 OBS 的「设置 → 输出」里把编码器换成 H.264（x264、NVENC H.264 等）。

**开播后 B 站显示的开播工具是「直播姬」**
插件就是以直播姬 PC 版的身份开播的，这是正常的。

## 注意

- 这是第三方工具，与哔哩哔哩无关。B 站随时可能修改接口，插件因此暂时失效时请到 [Issues](https://github.com/VOfficial404/obs-bililive/issues) 反馈。
- 使用第三方工具开播可能违反 B 站的用户协议，由此产生的风险由使用者自行承担。
- 登录凭据（`SESSDATA`、`bili_jct`）以明文保存在 OBS 配置目录下的 `plugin_config/obs-bililive/config.json`。不要把这个文件发给别人；不用了就在插件里点「退出登录」，凭据会被作废并清除。
- 插件只和 B 站的服务器通信，不收集任何数据。

## 从源码构建

本项目基于 [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)，依赖 libobs、obs-frontend-api、Qt 6（Core、Widgets、Network）和 libcurl。

```sh
# Windows（Visual Studio 2022）
cmake --preset windows-x64
cmake --build --preset windows-x64

# macOS（Xcode 16）
cmake --preset macos
cmake --build --preset macos

# Ubuntu 24.04
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
```

单元测试覆盖签名、弹幕协议和消息解析：

```sh
cmake --preset ubuntu-x86_64 -DENABLE_TESTS=ON
cmake --build --preset ubuntu-x86_64 --target bililive-tests
ctest --test-dir build_x86_64
```

更多说明见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 致谢

- [bilibili-API-collect](https://github.com/SocialSisterYi/bilibili-API-collect)：B 站接口文档
- [StartLive](https://github.com/Radekyspec/StartLive)：直播姬开播流程的参考实现
- [QR Code generator library](https://github.com/nayuki/QR-Code-generator)：二维码生成（MIT）

## 许可

[GPL-2.0-or-later](LICENSE)

---

## English

**BiliLive for OBS** lets you go live on Bilibili straight from OBS Studio. Bilibili only lets streamers with fewer than 5,000 followers start a broadcast from its LiveHime app; this dock starts the broadcast the way LiveHime does, writes the RTMP address into OBS and starts streaming.

Features: QR-code login, title and category, one-click go live / end broadcast (synced with OBS's own Start/Stop Streaming), face verification by QR code, a live feed of danmaku, gifts, Super Chats, guard purchases, follows and entries, viewer statistics, and sending danmaku.

Install the package for your platform from [Releases](https://github.com/VOfficial404/obs-bililive/releases/latest), restart OBS and open **Docks → Bilibili Live**. Requires OBS Studio 31 or newer.

This is an unofficial tool, not affiliated with Bilibili. Using it may breach Bilibili's terms of service; you use it at your own risk. Session cookies are stored in plain text in `plugin_config/obs-bililive/config.json` under the OBS configuration directory.

# 参与贡献

感谢你愿意改进 BiliLive for OBS。提交 Issue 或 Pull Request 前，请先读完这一页。

## 报告问题

- 先搜一下 [Issues](https://github.com/VOfficial404/obs-bililive/issues)，看看是不是已经有人报告过。
- 附上 OBS 版本、操作系统和插件版本。
- 附上 OBS 日志（「帮助 → 日志文件 → 上传当前日志文件」），插件的日志行以 `[obs-bililive]` 开头。
- **不要**贴出 `config.json`、Cookie 或推流码，它们能直接登录你的账号、往你的直播间推流。

## 开发环境

项目基于 [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)，CMake 预设会自动下载对应版本的 OBS 源码和依赖。

| 平台 | 工具 |
|---|---|
| Windows | Visual Studio 2022、CMake 3.30 |
| macOS | Xcode 16、CMake 3.30 |
| Ubuntu 24.04 | CMake 3.28、`ninja-build`、`build-essential`、`qt6-base-dev`、`libcurl4-openssl-dev`、OBS PPA 的 `obs-studio` |

```sh
cmake --preset windows-x64      # 或 macos / ubuntu-x86_64
cmake --build --preset windows-x64
```

## 代码结构

```
src/
  bili/          B 站 HTTP 接口、签名（APP 签名、WBI）
  danmaku/       弹幕协议：报文、zlib 解包、protobuf 读取、消息解析、TCP 连接
  ui/            停靠窗口和二维码对话框
  settings.*     配置读写
  stream-control.*  把推流地址写进 OBS
tests/           协议层的单元测试（不依赖 OBS）
deps/qrcodegen/  第三方二维码库
```

`bili/` 和 `danmaku/` 不依赖 libobs，组成静态库 `bililive-core`，单元测试只链接它。

## 测试

```sh
cmake --preset ubuntu-x86_64 -DENABLE_TESTS=ON
cmake --build --preset ubuntu-x86_64 --target bililive-tests
ctest --test-dir build_x86_64 --output-on-failure
```

改了签名、报文或消息解析，请补上对应的测试。B 站的 `*_V2` 消息是 protobuf，字段号是从实际流量里读出来的；新增字段时请附上一条抓到的样本（记得把用户名打码）。

## 代码风格

- C++17，格式由 `.clang-format`（clang-format 19）和 `.gersemirc`（CMake）决定，CI 会检查。
- 提交前运行 `./build-aux/run-clang-format` 和 `./build-aux/run-gersemi`。
- 界面文字放进 `data/locale/*.ini`，`zh-CN.ini` 和 `en-US.ini` 同时更新。

## 提交 Pull Request

- 一个 PR 只做一件事，标题说明做了什么。
- 用户能感觉到的变化，写进 `CHANGELOG.md` 的 `[Unreleased]` 一节。
- 提交 PR 即表示你同意以 [GPL-2.0-or-later](LICENSE) 授权你的代码。

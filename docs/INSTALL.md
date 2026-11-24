# 安装指南

插件需要 **OBS Studio 31 或更新版本**。版本号在 OBS 的「帮助 → 关于」里。

先在 [Releases](https://github.com/VOfficial404/obs-bililive/releases/latest) 下载你的系统对应的文件，然后按你用的 OBS 找到下面对应的一节：

| 你用的 OBS | 看哪一节 |
|---|---|
| Windows，官网下载的安装版 | [Windows：官网版和 Steam 版](#windows官网版和-steam-版) |
| Windows，Steam 上装的 | [Windows：官网版和 Steam 版](#windows官网版和-steam-版) |
| Windows，解压即用的便携版 | [Windows：便携版](#windows便携版) |
| macOS，官网版或 Steam 版 | [macOS](#macos) |
| Linux | [Linux](#linux) |

> **怎么知道是不是便携版？** 打开 OBS，看窗口标题栏：带「便携模式」（Portable Mode）字样的就是便携版。Steam 版和官网安装版默认都不是便携版；如果你在 Steam 的「属性 → 启动选项」里加过 `--portable` 或 `-p`，那它也是便携版。

## Windows：官网版和 Steam 版

Steam 版和官网版是同一个程序，读取插件的位置也一样，都是 `C:\ProgramData\obs-studio\plugins\`。插件**不要**放进 OBS 的安装目录（官网版的 `C:\Program Files\obs-studio\`，Steam 版的 `steamapps\common\OBS Studio\`）：OBS 33 已经把那里的第三方插件标成「旧版」，OBS 34 起就不再读取了。另外，Steam 卸载或重装 OBS 时会把整个安装目录删掉，放在 `ProgramData` 下的插件不受影响。

### 用安装程序（推荐）

1. 下载 `obs-bililive-*-windows-x64-setup.exe` 并双击运行，Windows 要求管理员权限时点「是」。
2. 安装目录保持默认的 `C:\ProgramData\obs-studio\plugins\obs-bililive`，一路点「下一步」。
3. 如果 OBS 正开着，安装程序会提示关闭它，选「自动关闭应用程序」即可。
4. 启动 OBS，在顶栏的 **B站直播 → 显示面板** 打开面板。

Windows 可能提示「Windows 已保护你的电脑」，这是因为安装程序没有付费的代码签名证书：点「更多信息 → 仍要运行」。

卸载：「设置 → 应用 → 已安装的应用」里找到「BiliLive for OBS」卸载。

### 手动安装（zip）

1. 关闭 OBS。
2. 解压下载的 `obs-bililive-*-windows-x64.zip`，会得到一个 `obs-bililive` 文件夹。
3. 在资源管理器的地址栏输入 `%ProgramData%\obs-studio\plugins` 并回车。
   - `ProgramData` 默认是隐藏文件夹，直接在地址栏输入就能打开。
   - 提示找不到时，先进入 `%ProgramData%`，再依次新建 `obs-studio` 和 `plugins` 两个文件夹。
4. 把第 2 步的 `obs-bililive` 文件夹整个复制进去。Windows 要求管理员权限时点「继续」。
5. 启动 OBS，在顶栏的 **B站直播 → 显示面板** 打开面板。

放好以后应该是这样：

```
C:\ProgramData\obs-studio\plugins\
└── obs-bililive\
    ├── obs-bililive.dll          ← OBS 33 及以后读这个
    ├── bin\64bit\obs-bililive.dll ← OBS 31、32 读这个
    └── data\locale\…
```

同一个文件放了两份是故意的：这样一个安装包能同时支持 OBS 31 到 33。OBS 33 读到第一份后会跳过第二份，日志里出现一行 `Skipping legacy plugin 'obs-bililive'` 是正常的。

### 用 Steam 版需要注意

- **自动更新不影响插件。** Steam 更新 OBS 时只替换安装目录，`ProgramData` 下的插件和插件的登录状态都会保留。
- **Steam 的测试版分支（beta）一样能用。** 测试版的 OBS 版本更新，按上面的步骤安装即可。
- **崩溃后的安全模式不加载插件。** OBS 崩溃后再启动会询问是否进入安全模式，选「正常启动」，否则面板不会出现。

## Windows：便携版

便携版不读 `ProgramData`，插件要放在 OBS 自己的文件夹里。下面的 `<OBS目录>` 指的是包含 `bin`、`data` 等文件夹的那一层。

用安装程序时，在「选择目标位置」这一步把目录改成 `<OBS目录>\plugins\obs-bililive` 即可（仅 OBS 33 及以后）。也可以按下面的方法手动放。

**OBS 33 及以后**：把解压得到的 `obs-bililive` 文件夹整个放进 `<OBS目录>\plugins\`（没有 `plugins` 文件夹就新建一个）：

```
<OBS目录>\plugins\obs-bililive\obs-bililive.dll
<OBS目录>\plugins\obs-bililive\data\locale\…
```

**OBS 31、32**：

1. 把 `obs-bililive\bin\64bit\obs-bililive.dll` 复制到 `<OBS目录>\obs-plugins\64bit\`；
2. 把 `obs-bililive\data\` 里的**内容**复制到 `<OBS目录>\data\obs-plugins\obs-bililive\`（这个文件夹需要新建）。

## macOS

官网版和 Steam 版的安装方法相同。

1. 关闭 OBS。
2. 双击 `obs-bililive-*-macos-universal.pkg`，按提示安装。插件会被装到 `~/Library/Application Support/obs-studio/plugins/obs-bililive.plugin`，只对当前用户生效。
3. 如果系统提示「无法打开，因为它来自身份不明的开发者」：打开「系统设置 → 隐私与安全性」，在页面底部点「仍要打开」，再双击一次安装包。
4. 启动 OBS，在顶栏的 **B站直播 → 显示面板** 打开面板。

## Linux

Steam 上没有 Linux 版 OBS（[OBS 官方说明](https://obsproject.com/kb/steam-faq)），所以 Linux 只提供 Ubuntu 的安装包。

- **Ubuntu 24.04，OBS 来自官方 PPA（`ppa:obsproject/obs-studio`）**：

  ```sh
  sudo apt install ./obs-bililive-*-x86_64-linux-gnu.deb
  ```

- **Flatpak 版 OBS**：暂不支持。Flatpak 版只加载以 Flatpak 扩展形式发布的插件。
- **其他发行版**：安装包是按 Ubuntu 24.04 的系统库编译的，请参照 [CONTRIBUTING.md](../CONTRIBUTING.md) 从源码编译。

## 确认装好了

满足下面任意一条，就说明插件已经加载：

- OBS 的「停靠窗口」菜单里有「B站直播」；
- 「帮助 → 日志文件 → 查看当前日志」里能搜到 `[obs-bililive] plugin loaded successfully`。

在 OBS 33 及以后，「工具」菜单里的插件管理器也会列出 obs-bililive。如果它被标成旧版（Legacy），说明插件放在了旧位置，请按上面的步骤移到新位置。

## 装了却看不到面板

- **文件夹套了两层。** 解压工具有时会多建一层，变成 `plugins\obs-bililive\obs-bililive\…`。`obs-bililive.dll` 必须在 `plugins\obs-bililive\` 下面，不能更深。
- **放错了位置。** 便携版要放在 OBS 自己的文件夹里，不读 `ProgramData`。
- **OBS 版本太旧。** 需要 31 或更新版本。
- **进了安全模式。** 安全模式下不加载第三方插件，请正常重启 OBS。
- **面板被关掉了。** 在顶栏的 **B站直播 → 显示面板** 重新打开，或在「停靠窗口」菜单里重新勾选「B站直播」。

还是不行的话，请在 [Issues](https://github.com/VOfficial404/obs-bililive/issues) 里附上 OBS 日志（「帮助 → 日志文件 → 上传当前日志文件」）。

## 更新和卸载

- **更新**：插件启动时会检查新版本，有新版本时「B站直播」菜单最上面会出现「发现新版本」，点它打开下载页。Windows 下载新的 `-setup.exe` 直接运行，会装到原来的位置；用 zip 的话，关闭 OBS 后用新文件覆盖旧文件。
- **卸载**：先在插件里「退出登录」，让登录凭据失效；然后在 Windows「已安装的应用」里卸载「BiliLive for OBS」，手动安装的删除上面安装的 `obs-bililive` 文件夹（macOS 是 `obs-bililive.plugin`，Ubuntu 用 `sudo apt remove obs-bililive`）。
- **插件的配置和登录凭据**存放在：
  - Windows：`%APPDATA%\obs-studio\plugin_config\obs-bililive\`（便携版在 `<OBS目录>\config\obs-studio\plugin_config\obs-bililive\`）
  - macOS：`~/Library/Application Support/obs-studio/plugin_config/obs-bililive/`
  - Linux：`~/.config/obs-studio/plugin_config/obs-bililive/`

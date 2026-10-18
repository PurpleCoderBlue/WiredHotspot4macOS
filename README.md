# 有线热点4macOS

**Wired Hotspot 4 macOS**

[简体中文](README.md) · [English](README.en.md)

面向 macOS 的 Android USB 网络共享工具，基于 [HoRNDIS Userspace](https://github.com/noahhhi/HoRNDIS-Userspace) 维护。本分支默认使用简体中文，补充了权限边界、USB 传输、连接失败回滚和断开清理的修复及回归测试。

**当前版本：0.1.0，本项目从此独立编号，不跟随上游版本。仅实现 RNDIS，不提供 ECM/NCM 传输。**

[用户手册](docs/USER_GUIDE.zh-CN.md) · [反馈问题](docs/BUG_REPORTING.zh-CN.md) · [权限模型](docs/PRIVILEGE_MODEL.md) · [限制与真机验收](docs/LIMITATIONS.md)

## 下载安装

1. 从 [本仓库 Releases](https://github.com/PurpleCoderBlue/WiredHotspot4macOS/releases/latest) 下载 `WiredHotspot4macOS-v0.1.0-macos-universal.pkg`。
2. 可用同页的 `SHA256SUMS` 核对文件：`shasum -a 256 -c SHA256SUMS`。
3. 打开 PKG，按提示完成管理员认证。
4. 连接 Android 手机，在手机上开启“USB 网络共享”。

安装包包含 Apple Silicon 和 Intel 两种架构。命令仍为 `horndis`，菜单栏 App 仍安装到 `/Applications/HoRNDIS Status.app`；沿用原有服务标识，便于升级已有安装。0.1.0 起独立编号；明确运行安装包时会替换原 App，即使已有上游版本号更高。

> 安装包未使用 Developer ID 签名或公证，内置程序使用本地 ad-hoc 签名。如果 macOS 阻止打开，请确认下载来源后，在“系统设置 → 隐私与安全性”中使用“仍要打开”。无需关闭 SIP 或降低启动安全性。

也可以通过[本项目自建的 Homebrew Tap](https://github.com/PurpleCoderBlue/homebrew-tap)安装同一份正式版 PKG：

```sh
brew tap PurpleCoderBlue/tap
brew install --cask purplecoderblue/tap/wiredhotspot4macos
```

这是项目维护者提供的第三方 Tap，不属于 Homebrew 官方仓库；安装仍需管理员认证，签名状态与直接下载的 PKG 相同。上游 Tap 安装的是上游版本，请勿同时管理同一份安装。

## 使用与语言

菜单栏可暂停/恢复共享、设置登录时启动、查看连接信息和流量、保存诊断报告。退出菜单栏 App 不会停止后台网络服务。

**App 默认使用简体中文，不修改系统语言设置。** 如需其他语言，可在“系统设置 → 通用 → 语言与地区 → 应用程序”中为有线热点4macOS明确选择语言，然后重新打开 App。保留英语、繁体中文、日语、韩语、法语、德语、西班牙语、巴西葡萄牙语、意大利语及俄语资源。

```sh
horndis probe                 # 查看 USB 网络功能
horndis status                # 查看服务与连接状态
horndis diagnostics report.txt # 生成脱敏诊断报告
horndis start                 # 打开菜单栏 App
horndis uninstall             # 卸载持久服务和菜单栏启动项
```

服务日志：`/var/log/horndis.log`。安装、升级和卸载需要管理员认证，日常菜单操作不需要。

手机是否自动开启 USB 网络共享由手机系统决定。本程序不使用 ADB，也不自动打开手机热点。

## 本分支的修复

- USB/RNDIS 数据进程永久降权；限制继承的 BPF 描述符重新绑定，并在控制台用户切换时撤销旧会话。
- root 状态文件使用独立目录及基于文件描述符的发布操作，避免用户目录中的符号链接引导特权写入。
- 本地控制 socket 支持分段命令，并限制客户端数量和等待时间。
- 修复 RNDIS USB 短包补零、收发容量混用、异步状态消息和复合 USB 设备接口配对。
- DHCP 启动失败时回滚自建网络；断开清理有界重试，修复“已提交但未应用”之后重试误报成功的问题。
- 先取消 USB 写入并请求撤网，再等待工作线程；队列等待和 supervisor 请求增加截止时间，延迟回调保留独立生命周期。

只管理本程序创建的 bridge/feth 网络，不修改 Wi-Fi、系统代理、Clash 或网络服务优先级。网络偏好由 macOS `configd` 应用，仍有系统依赖；详细边界见[权限模型](docs/PRIVILEGE_MODEL.md)。

## 验证范围与代理兼容

本地完整回归包含 21 个测试程序，覆盖协议、USB 替身、网络故障注入、控制通道、状态文件、权限限制及菜单规则；USB 回归还运行了 ASan/UBSan。测试不等于全部设备验收。

本地荣耀 500 Pro（RNDIS）已验证 DHCP 和绑定 USB 接口的真实 IP HTTPS 访问。睡眠唤醒、长时间高负载、更多手机/macOS 组合及实际用户切换仍需真机验证。上游报告的 Pixel 4 XL 测试记录不等于本分支重新执行过同一套验收。

若启用了 Clash/Mihomo TUN，仅看到系统默认路由切换不能证明代理已切换出口。本地曾复现：仅 USB 时直连 HTTPS 成功，但 Mihomo 仍选择已断开的 Wi-Fi 接口，TUN 和代理请求超时。本程序不自动更改代理设置；排查时应分别检查 USB 直连和代理出口。

## 源码编译

需要 macOS、Xcode 或 Command Line Tools 和 Make。二进制最低目标为 macOS 11；旧系统和 Intel 的实际兼容性仍以真机结果为准。

```sh
make
make test                    # 以普通用户运行，不安装服务或修改系统网络
```

也可打开 `WiredHotspot4macOS.xcodeproj`，选择 **WiredHotspot4macOS → Build**。**Regression Tests → Build** 调用 Makefile 测试，不使用 XCTest。输出按配置和架构分离，位于 `build/xcode/`。

构建通用安装包：

```sh
make all BUILD_DIR=build/release ARCH_FLAGS='-arch arm64 -arch x86_64' STATUS_ARCHS='arm64 x86_64'
BUILD_DIR="$PWD/build/release" Scripts/build-package 0.1.0 build/WiredHotspot4macOS-v0.1.0-macos-universal.pkg
```

打包会移除本机调试路径及 AppleDouble 元数据并重新校验签名。可用 `python3 Tests/PackageContentsTests.py <安装包.pkg>` 检查版本、架构、所有权及包内容，无需安装。

构建和测试不会安装服务；从源码安装需另行显式运行安装命令。

## 来源与许可证

基于 [noahhhi/HoRNDIS-Userspace](https://github.com/noahhhi/HoRNDIS-Userspace)，保留原作者及贡献者版权和 [GPL-3.0-or-later 许可证](LICENSE)。这是独立维护分支，不代表上游官方发布。

# Wired Hotspot 4 macOS

有线热点4macOS

[简体中文](README.md) · [English](README.en.md)

An independently maintained fork of [HoRNDIS Userspace](https://github.com/noahhhi/HoRNDIS-Userspace) for Android USB tethering on macOS. This fork defaults to Simplified Chinese and includes fixes and regression coverage for privilege boundaries, USB transfers, activation rollback and disconnect cleanup.

**Version 0.1.0 is the first independently numbered release of this fork. Only RNDIS is implemented; ECM/NCM transports are not included.**

[User guide](docs/USER_GUIDE.md) · [Report a bug](docs/BUG_REPORTING.md) · [Privilege model](docs/PRIVILEGE_MODEL.md) · [Limitations and hardware acceptance](docs/LIMITATIONS.md)

## Install

Download `WiredHotspot4macOS-v0.1.0-macos-universal.pkg` and `SHA256SUMS` from [this repository's Releases](https://github.com/PurpleCoderBlue/WiredHotspot4macOS/releases/latest). Verify with `shasum -a 256 -c SHA256SUMS`, open the package, complete administrator authentication, and enable USB tethering on Android.

The package includes Apple Silicon and Intel binaries. The command remains `horndis`, and the menu app remains `/Applications/HoRNDIS Status.app`. Existing service identifiers are retained for upgrades. Version 0.1.0 starts an independent numbering scheme; an explicit installation replaces an older upstream app even if its version number is higher. The upstream Homebrew tap installs the upstream version, not this fork.

The installer has no Developer ID signature or notarization; its binaries are ad-hoc signed. If macOS blocks opening it, verify its origin and use **System Settings → Privacy & Security → Open Anyway**. SIP and boot security do not need to be reduced.

## Use and language

The menu controls pause/resume, login startup, connection details, traffic totals and diagnostic export. Quitting the menu app does not stop the network service. Install, upgrade and uninstall require administrator authentication; normal menu operations do not.

**The app defaults to Simplified Chinese without changing system preferences.** An explicit per-app language selection in **System Settings → General → Language & Region → Applications** takes precedence after reopening the app. All eleven existing localizations remain available.

```sh
horndis probe
horndis status
horndis diagnostics report.txt
horndis start
horndis uninstall
```

Service log: `/var/log/horndis.log`. Whether USB tethering turns on automatically is controlled by Android. This program does not use ADB or turn on the phone's hotspot.

## Fixes in this fork

- Permanent USB-agent privilege drop, inherited BPF reconfiguration restrictions and console-session capability revocation.
- Separate root status directory with descriptor-based publication; bounded and fragmented local control commands.
- RNDIS USB short-packet padding, separate RX/TX capacities, asynchronous status handling and composite-device interface association.
- Activation rollback, bounded suspension retries and reapplication after preferences commit succeeds but apply fails.
- USB cancellation and network suspension before worker join; request deadlines and independent lifetime for late USB completions.

The service manages only its own bridge/feth network and does not change Wi-Fi, system proxy, Clash or service order. Apple's private bridge APIs and synchronous `configd` calls remain platform dependencies; see the [privilege model](docs/PRIVILEGE_MODEL.md).

## Validation and proxy compatibility

The local suite has 21 regression executables, including protocol, USB doubles, network fault injection, supervisor, runtime files, sandbox and menu checks. USB regressions also pass ASan/UBSan. Hardware checks remain separate.

An Honor 500 Pro has been checked locally for RNDIS DHCP and USB-bound real-IP HTTPS. Sleep/wake, sustained load, other phone/macOS combinations and actual console-user switching still require acceptance testing. Pixel 4 XL results documented by upstream were not rerun as this fork's acceptance suite.

A local USB-only handover test reproduced successful direct USB HTTPS while Mihomo kept selecting the disconnected Wi-Fi interface and TUN/proxy requests timed out. The system default route alone does not prove a proxy has changed outbound interfaces. This program does not automatically reconfigure proxies.

## Build

Use macOS, Xcode or Command Line Tools, and Make. The binary deployment target is macOS 11; older-system and Intel runtime compatibility still need hardware checks.

```sh
make
make test    # non-root; does not install services or modify system networking
```

Open `WiredHotspot4macOS.xcodeproj` and build the **WiredHotspot4macOS** scheme, or build **Regression Tests** to run the Makefile suite. These tests are standalone executables, not XCTest. Xcode outputs are separated by configuration and architecture under `build/xcode/`.

```sh
make all BUILD_DIR=build/release ARCH_FLAGS='-arch arm64 -arch x86_64' STATUS_ARCHS='arm64 x86_64'
BUILD_DIR="$PWD/build/release" Scripts/build-package 0.1.0 build/WiredHotspot4macOS-v0.1.0-macos-universal.pkg
```

The package build strips local debug paths, removes AppleDouble metadata and verifies signatures. Check a built package without installing it with `python3 Tests/PackageContentsTests.py <package.pkg>`.

Building and testing do not install the service. Installation from source is a separate explicit operation.

## Attribution and license

Based on [noahhhi/HoRNDIS-Userspace](https://github.com/noahhhi/HoRNDIS-Userspace). Original author/contributor notices and the [GPL-3.0-or-later license](LICENSE) are retained. This fork is not an official upstream release.

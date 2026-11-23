# BRT 的 GitHub 资源来源

这份清单对应 BRT 19.20.34 启动器与本地 PvE 玩法组件。安装器下载的社区发行包、用于重建玩法 DLL 的源码，以及编译启动器的库，各有独立版本。

| 项目 | BRT 怎样使用 | 版本与本仓库依据 |
| --- | --- | --- |
| [STanJK/rust-boundary-tool-box](https://github.com/STanJK/rust-boundary-tool-box) | Windows 工具箱基底，提供 Rust／Slint 界面、安装及联机代码。本地浅克隆最早可见的提交也存在于该上游。 | [共同提交 edaca53](https://github.com/STanJK/rust-boundary-tool-box/commit/edaca5380409ff2a99bc295d041706b4d394469f) · [项目说明](../README.md) |
| [STanJK/ProjectRebound](https://github.com/STanJK/ProjectRebound) | 安装器从 Stable 包取基础文件，从 V0.8.4 包取社区玩法 DLL、专服启动器和随附的档案服务。本地 PvE 的定制 DLL 另在上游 C++ 工程和 SDK 上应用补丁后编译。 | [Stable](https://github.com/STanJK/ProjectRebound/releases/tag/Stable) · [V0.8.4](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4) · [live38 源码基线 74c68ac](https://github.com/STanJK/ProjectRebound/commit/74c68ac3e6007c36248c442a281c2b7b831cc53a) · [安装器](../src/core/community_install.rs) · [玩法构建脚本](../integrations/gameplay/community-stack/build-payload.ps1) |
| [STanJK/BoundaryMetaServer](https://github.com/STanJK/BoundaryMetaServer) | Node.js 档案服务，负责玩家角色和配装。BRT 的 V0.8.4 适配从 ProjectRebound 的指定发行包提取 44 个 JS 和协议文件，随适配组件保存上游 AGPLv3 许可证。独立仓库的源码版本也用于兼容性核对。 | [独立仓库版本 007850d](https://github.com/STanJK/BoundaryMetaServer/commit/007850d0fd07c688d0a1ec1ff2a8771726c3463c) · [发行包取材脚本](../integrations/profile-runtime/pack-release.cjs) · [随附许可证](../integrations/profile-runtime/release-084-LICENSE.txt) |
| [vnt-dev/vnt](https://github.com/vnt-dev/vnt) | 联机页面使用的组网与 P2P 核心。BRT 直接编译本地的 vnt-ipc，后者依赖同一份 vnt-core。 | [v2 源码 e088d30](https://github.com/vnt-dev/vnt/commit/e088d305b1246086c3c16d54edf764effc68db6d) · [启动器 Cargo 清单](../Cargo.toml) · [IPC 依赖](../vendor/vnt/vnt-ipc/Cargo.toml) · [Apache 2.0 许可证](../vendor/vnt/LICENSE) |
| [slint-ui/slint](https://github.com/slint-ui/slint) | 启动器的原生界面框架。开发依赖来自 Cargo，构建脚本使用 slint-build 编译界面。 | [Cargo 依赖](../Cargo.toml) · [界面入口](../ui/appwindow.slint) |
| [subframe7536/maple-font](https://github.com/subframe7536/maple-font) | 界面声明 Maple Mono CN 为首选字体。字体安装代码能从该项目的 Release 选择中文字体包。 | [界面字体声明](../ui/appwindow.slint) · [字体包选择逻辑](../src/core/font.rs) |
| [nodejs/node](https://github.com/nodejs/node) | 运行本地档案服务。BRT 社区安装器实际从 Node.js 官方分发站获取 Windows x64 版 24.22.3，并核验 SHA-256。 | [官方下载目录](https://nodejs.org/dist/v24.22.3/) · [版本与校验值](../src/core/community_install.rs) |

VNT 在当前 BRT 锁文件中还引入以下网络库。其余常规 Rust 依赖可按包名和版本查阅完整锁文件。[VNT 上游库说明](../vendor/vnt/README.md) · [BRT 完整依赖版本](../Cargo.lock)

| VNT 的上游 | 用途 | 获取方式 |
| --- | --- | --- |
| [rustp2p/tcp_ip](https://github.com/rustp2p/tcp_ip) | 用户态 TCP/IP 协议栈 | Git 固定提交 [09c64c8](https://github.com/rustp2p/tcp_ip/commit/09c64c81da70a35248dfef4ef42bb9f5e8b6e05e) |
| [rustp2p/rustp2p](https://github.com/rustp2p/rustp2p) | P2P 通道与打洞 | Cargo 注册表的 rust-p2p-core |
| [tun-rs/tun-rs](https://github.com/tun-rs/tun-rs) | 虚拟网卡接口 | Cargo 注册表的 tun-rs |
| [tun-rs/route_manager](https://github.com/tun-rs/route_manager) | 路由管理 | 通过 tun-rs 间接使用 |

**协议参考。** 排查大厅地区列表时使用了独立的 Rust 服务 MetadataServerV2，对照地区查询 RPC 的状态码和 Protobuf 消息结构。实际档案服务仍使用表中的 Node.js 实现。[ProjectRebound/MetadataServerV2 对照版本](https://github.com/ProjectRebound/MetadataServerV2/tree/a239712be2a6df5966b8be1e20eef654bae8f2ee) · [本项目的对照记录](../integrations/native-menu/REGION-QOS.md)

**历史更新地址。** 基底代码保留了 LinchenFur/rust-boundary-tool-box 的 Release 查询地址。BRT 独立发行版通过开关关闭该更新流程。本次匿名访问仓库返回 404，无法据此判断其可见性或归属。[旧更新地址](../src/app/update.rs) · [手动更新逻辑](../src/app/updates.rs)

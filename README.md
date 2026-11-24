# Boundary Resurrection Toolkit（BRT）

BRT 是为 Steam《边境》（Boundary）玩家制作的非官方 Windows 工具箱。它把社区组件安装、本机开房和原版大厅连在一起：你可以在大厅里选干员、调整配装，再用这套配装进入本机人机对局。压缩包不含游戏本体，需要先用拥有《边境》的 Steam 账号安装游戏。

**[下载 Windows x64 玩家工具箱（ZIP，19.20.38 预览版）](https://github.com/teacateet/BoundaryToolbox/releases/download/v19.20.38/BRT-19.20.38-preview-win64.zip)** · [版本说明与校验值](https://github.com/teacateet/BoundaryToolbox/releases/tag/v19.20.38) · [玩家指南](docs/PLAYER_GUIDE.md)

## 开始一局本机人机

1. 确认已安装 Steam 版《边境》**Build 14530908**。退出游戏、旧工具箱和专服，把下载的 ZIP 完整解压到新文件夹。
2. 打开新文件夹里的 `BRT.exe`，确认 Steam 游戏目录，点一次 **「安装社区环境」**。首次安装需要联网；从 19.20.35、19.20.36 或 19.20.37 升级也要点这一步。看到 `live42` 已就绪或已安装后再继续。
3. 打开 **「本机人机匹配」→「进入原版大厅」**，在游戏里选 **「人机对战」** 和难度。首次提示选择地区时选「东亚（香港）」。点匹配后保持工具箱开着，等地图加载、角色出生。

## 它是怎么工作的？

| 环节 | BRT 的做法 |
| --- | --- |
| 安装与升级 | Rust／Slint 启动器识别 Steam 安装目录，下载固定版本的社区基础组件并核对 SHA-256。玩法修复 live42 随 ZIP 分发。升级已识别的旧版本会备份原 DLL，保留玩家配装和房间设置。 |
| 从大厅进入对局 | 本机运行 Node.js 档案服务，把角色、武器和配件提供给原版大厅。玩家选好难度后，工具箱启动对应地图的社区专服，等房间就绪，再把这次匹配交给它。 |
| 首次出生与死亡换人 | 首命把镜头交回角色；主枪已进背包但双手持续空闲时，才触发原有装备动作。死亡后按 Esc 换人，若菜单已关但角色迟迟没有出生，本机 PvE 专服核对本次部署并走原生复活流程。客户端等玩家重新接管角色后，再恢复移动和转向。 |
| 钩索再次使用 | 专服等当前地图的网络驱动就绪，再将它绑定到该地图的关卡集合。钩索仍按游戏原有的时间冷却，客户端收到结束通知后可以再次出钩。 |
| 和朋友联机 | 同一局域网：房主开房后分享自己的 IP 和游戏端口，队友在「房间」直连。异地：双方先在「网络」连接相同组网服务器和房间号，再由房主开游戏房、队友选房主节点加入。建房和组网都可以取消；失败后可改参数重试。 |

**上版首命与钩索验收：** 客户端和专服都使用 19.20.37 玩家 ZIP。在普通难度 MiniFarm，用鼠标点选并部署探针（PROBE）：首命不切枪即可正常看到配装的 DMR 和瞄具；第二条命等待原有冷却后，两次出钩都连上锚点。已验证全新安装、从公开 19.20.35 和 19.20.36 升级及保留配装与房间配置。[19.20.37 首命前后截图与钩索记录](docs/BRT-LIVE41.md) · [前版 CircularX 死亡换人的键鼠实测](docs/BRT-LIVE39.md)

本版换人和联机取消的实测步骤与范围见 [19.20.38 验收记录](docs/BRT-LIVE42.md)。

## 使用的社区项目

感谢下面这些上游项目。BRT 复用了社区已有的启动器、专服和档案服务，在它们之上补了本机匹配、配装衔接、玩法修复和安装校验。

| 项目 | 在 BRT 中的用途 |
| --- | --- |
| [STanJK/rust-boundary-tool-box](https://github.com/STanJK/rust-boundary-tool-box) | Windows 工具箱基底，提供 Rust／Slint 界面及安装、联机代码。 |
| [STanJK/ProjectRebound](https://github.com/STanJK/ProjectRebound) | 社区运行环境和专服启动器。BRT 使用 Stable、V0.8.4 发行组件；live42 在其 C++ 源码和 SDK 上构建。 |
| [STanJK/BoundaryMetaServer](https://github.com/STanJK/BoundaryMetaServer) | 角色和配装档案服务。BRT 适配 ProjectRebound V0.8.4 随附的版本。 |
| [vnt-dev/vnt](https://github.com/vnt-dev/vnt) | 联机页的虚拟组网和 P2P 通道。 |
| [slint-ui/slint](https://github.com/slint-ui/slint) · [nodejs/node](https://github.com/nodejs/node) · [subframe7536/maple-font](https://github.com/subframe7536/maple-font) | 分别用于桌面界面、档案服务运行时，以及可选的中文字体。 |

[完整资源清单与固定版本](docs/UPSTREAM-SOURCES.md)列出了直接使用的项目、VNT 的间接依赖，以及只用于协议对照的仓库。第三方组件的来源和许可证说明见[随包来源清单](tools/component-sources.txt)。

## 从源码构建启动器

在 Windows x64 上准备 Rust（MSVC 工具链）、Node.js 和 `tar`。下载 [ProjectRebound V0.8.4 的 Release.zip](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4)，从仓库根目录运行：

```powershell
node integrations/profile-runtime/pack-release.cjs "C:\Downloads\Release.zip"
cargo build --locked --release --bin BRT
```

第一条命令先核对上游包的 SHA-256，再提取编译所需的档案服务源码；第二条生成 `target/release/BRT.exe`。只想游玩的玩家请使用上方完整 ZIP。开发者可继续阅读[项目架构](docs/ARCHITECTURE.md)和[代码地图](docs/CODE_MAP.md)。

遇到问题可以[提交 Issue](https://github.com/teacateet/BoundaryToolbox/issues)，注明工具箱版本、地图、难度和出现问题的步骤。截图或日志请先遮住 Steam 账号、外网地址和房间密码。

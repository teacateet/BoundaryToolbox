# Boundary Resurrection Toolkit

**BRT 工具箱**是基于 Boundary 社区工具箱独立开发的 Windows 启动器，运行玩家自己安装的 Steam 版《边境》。从[玩家安装与联机指南](docs/PLAYER_GUIDE.md)开始；新玩家可以从出发页安装社区组件。BRT 的启动页、标识和验收工作由本项目维护，社区对局组件来源与许可见[来源清单](tools/component-sources.txt)。

## 近期更新

- 开放 VNT 联机页面，移除暂未开放遮罩，可直接配置并启动联机平台。
- 联机页新增 P2P 直连 / 中转连接选项，支持按网络环境切换连接方式。
- 优化联机页布局和节点列表显示，提升运行状态、服务器和联机节点信息的可读性。
- 下载代理选择增加自动选择入口，并按测速延迟优先排列可用节点。

## 快速入口

- 架构说明：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- 代码地图：[docs/CODE_MAP.md](docs/CODE_MAP.md)
- 代码规范：[docs/STYLE.md](docs/STYLE.md)
- 本地开发：[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)
- 发布流程：[docs/RELEASE.md](docs/RELEASE.md)
- 常见问题排查：[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

## 技术栈

- UI：Slint 1.16
- 主逻辑：Rust 2024
- 网络：blocking `reqwest`，后台线程执行
- 压缩包：`zip`
- 运行时诊断：`sysinfo`、`netstat2`
- Windows 集成：`windows`、`winreg`
- VNT 联机：本地 vendored `vendor/vnt`

## 常用命令

```powershell
cargo fmt --check
cargo test --locked --bin BRT
cargo build --locked --release --bin BRT
```

此分支默认生成独立启动器 `target/release/BRT.exe`。玩家测试包不内嵌私有载荷、Node.js、玩家档案或游戏文件。验收边界见[协作交接](docs/HANDOFF.md)。

## 重要约束

- 不要在 Slint 回调里做耗时文件、网络或进程操作；放到后台线程，通过 `AppMessage` 回到 UI。
- 不要把在线下载的 ProjectRebound、BoundaryMetaServer、Node.js 目录放进内嵌 payload。
- BRT 使用手动更新，与基底的同版本发布资产相互独立。
- 不要上传 PowerShell 脚本或本地临时构建文件。


## 从源码构建

首次编译前，请从 [ProjectRebound V0.8.4](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4) 获取 Release.zip，在仓库根目录运行下面的命令。命令核对上游 SHA-256，再生成档案服务所需的公开源码组件。生成文件仅保留于本机。

    node integrations/profile-runtime/pack-release.cjs <Release.zip的路径>
    cargo build --locked --release --bin BRT

运行特定实机测试时，向对应脚本传入 Steam 安装目录、固定版本的社区 SDK 或档案服务目录。

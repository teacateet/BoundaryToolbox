# BRT 工具箱本地测试包

目标文件为 `BRT.exe`。Windows x64 便携测试包可给已安装 Steam 原版的玩家补齐社区组件，默认不上传、不自更新，也不内嵌游戏或私有安装载荷。基底的历史流程另见 [UPSTREAM_RELEASE.md](UPSTREAM_RELEASE.md)。

## 构建与打包

在已具备本项目 Rust/Windows 构建环境的机器上，从仓库根目录运行：

```powershell
cargo test --release --locked --bin BRT
cargo build --release --locked --bin BRT
pwsh -File tools/package-preview.ps1
```

保留既有玩家目录时，传入独立输出目录，例如 `pwsh -File tools/package-preview.ps1 -OutputDirectory target/player-preview/19.20.20`。将生成的 ZIP 解压到另一个空目录验证，不要在正在使用的工具箱目录就地测试。

打包脚本使用 PowerShell 7。它从 [玩家指南](PLAYER_GUIDE.md) 渲染可离线阅读的 HTML，检查程序的 Windows 产品名和原始文件名，并将程序、指南、第三方组件来源和许可、配套档案服务源码与补丁及 SHA-256 清单放入 zip。脚本重新打开 zip，逐项校验内容后才输出到 `target/player-preview/`。发放前核对 zip 摘要，并按实际结果更新玩家指南的“本轮测试范围”。不随包附带工作目录、日志、Steam 个人数据或玩家装备档案。

## 发布门槛

1. 用实际 BRT 构建渲染入口与服务器页，核对名称、中文按钮、端口冲突和错误状态。
2. 核对 [ProjectRebound 发布页](https://github.com/STanJK/ProjectRebound/releases)与 [Node.js 官方摘要](https://nodejs.org/dist/v24.22.3/SHASUMS256.txt)。本机已用仅含原版主程序的目录验证干净安装、实际 Node 加载、档案适配和重复安装。公开发布前仍须在另一台玩家电脑上走通 Steam 登录、完整安装、进入大厅并实际进场。
3. 验证原版大厅改枪、出生后装备和挂载、原生 PvE/教程切图、局内雷达/F2/Esc。使用两台设备验证同一场内的直连与 P2P。
4. 发布地址和打包授权由维护者确认。内测包和基底工具箱不同名，不将其推送到基底仓库的发布渠道。

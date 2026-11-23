# 架构说明

本文说明 Boundary 社区服工具箱的主要边界和数据流。读代码前建议先看这份文档，再配合 [CODE_MAP.md](CODE_MAP.md) 定位具体文件。

## 总体分层

```text
main.rs
  |
  |-- ui/*.slint              Slint 入口、共享模型和 i18n
  |-- ui/components/*         可复用基础控件和展示容器
  |-- ui/dialogs/*            独立弹窗
  |-- ui/pages/*              顶级页面
  |-- ui/rows/*               列表行组件
  |
  |-- src/app/*               Slint 控制器层：绑定回调、更新 UI、调度后台任务
  |
  |-- src/core.rs + core/*    核心业务层：文件系统、安装、启动、卸载、下载、诊断
  |
  |-- src/vnt_platform.rs     VNT 联机平台适配层
```

核心原则：

- UI 层不直接改文件、不直接跑网络请求、不直接启动长耗时流程。
- `app` 层只负责把用户操作翻译成后台任务，并把结果应用回 Slint 模型。
- `core` 层是高风险操作边界，所有安装目录写入、进程结束、下载校验都在这里收敛。
- 需要跨线程回 UI 的结果统一通过 `AppMessage`。

## 入口与生命周期

`src/main.rs` 只做三件事：

1. 设置 Windows release 模式隐藏控制台。
2. `slint::include_modules!()` 引入 Slint 编译产物。
3. 调用 `app::run()`。

`src/app/mod.rs::run()` 负责：

1. 安装日志过滤。
2. 创建 `AppWindow`。
3. 创建 `AppController`。
4. 绑定 Slint 回调。
5. 启动后台定时器。
6. 运行 Slint event loop。
7. 窗口退出时停止后台线程。

## UI 线程与后台线程

Slint UI 必须保持响应。所有耗时操作都应遵循下面模式：

```text
Slint callback
  -> AppController 方法
  -> thread::spawn
  -> core 执行业务
  -> tx.send(AppMessage::...)
  -> AppController::drain_messages()
  -> 更新 Slint 属性 / VecModel
```

当前主要后台任务：

- 安装 / 卸载 / 启动
- 游戏进程和端口检测
- 服务器列表刷新
- GitHub 代理列表测速
- 更新检查和下载
- 系统环境检查
- VNT 联机状态同步

注意：如果以后改成 `slint::invoke_from_event_loop`，仍要保持“后台执行，UI 只刷新状态”的边界。

## UI 模型策略

Slint 列表模型由 Rust 持有：

- `port_model`
- `server_model`
- `github_proxy_model`
- `vnt_server_model`
- `vnt_peer_model`

这些模型在 `AppController::new()` 中创建一次。后续更新尽量使用 `VecModel` 原地修改，避免替换模型造成 ListView 绑定失效或 UI 抖动。

## 安装流程

安装主入口是 `InstallerCore::install_with_progress()`。

高层流程：

1. 校验内嵌 payload 是否包含本地必需文件。
2. 校验目标目录是否是 Boundary 的 `Binaries\Win64`。
3. 获取 ProjectRebound 最新 Release tag。
4. 下载 ProjectRebound `Release.zip`。
5. 下载 BoundaryMetaServer 源码包。
6. 下载 Node.js Windows 运行时。
7. 关闭目标目录相关运行进程。
8. 清理旧配置和历史残留。
9. 按 `MANAGED_ITEMS` 写入文件、备份原有内容、生成状态记录。
10. 写入 `.pve_installer/state.json` 和 `markers.json`。

三类载荷的处理方式不同：

- 内嵌 payload：`DT_ItemType.json`、`dxgi.dll`、`steam_appid.txt`
- ProjectRebound 在线文件：`Payload.dll`、`ServerLauncher/*`、`project_rebound_version.txt`
- 独立在线依赖：`BoundaryMetaServer-main`、`nodejs`

这三类不要混淆。分类错误会直接导致安装缺文件或启动失败。

## 卸载与恢复

卸载入口是 `InstallerCore::uninstall()`。

优先读取 `.pve_installer/state.json`，按安装记录反向删除并恢复备份。如果状态文件不存在，会退回到受控清理逻辑。这个设计避免误删用户目录里的无关文件。

## 启动流程

启动入口是 `InstallerCore::launch()`。

PVP：

1. 检查 Steam 在线状态。
2. 校验登录服务器、Node.js、游戏 exe。
3. 启动 BoundaryMetaServer。
4. 启动游戏并传入 `-LogicServerURL=http://127.0.0.1:8000`。

PVE：

1. 执行 PVP 的登录服务器前置流程。
2. 校验并启动 `ServerLauncher/ProjectReboundServerLauncher.exe -cli`。
3. 启动游戏并额外传入 `-match=127.0.0.1`。

启动失败优先看 `installer_tool/logs/installer_*.log`。

## 更新流程

更新检查在 `src/app/update.rs`：

- 查询 GitHub Release。
- 比较 `APP_VERSION` 和最新 tag。
- 优先选择名为 `boundary_toolbox.exe` 的资产。
- 下载到临时目录。
- 生成外部更新助手，等待当前进程退出后替换 exe。

Release 上传资产必须保留 `boundary_toolbox.exe` 名称，否则自动更新会退回到第一个 `.exe` 资产，容易下载到错误文件。

## VNT 联机

VNT 代码在 `src/vnt_platform.rs` 和 `src/app/vnt_controller.rs`。

- `vnt_platform` 负责调用 vendored VNT 核心、维护会话和事件。
- `vnt_controller` 负责把 VNT 事件转换成 UI 文本和列表模型。
- VNT 设置持久化在 `AppPrefs` 中。

## 发布构建

构建脚本 `build.rs` 会：

1. 编译 Slint。
2. 打包内嵌 payload。
3. 生成 Windows 图标资源。

正式发布时必须设置 `BOUNDARY_PAYLOAD_ROOT`。否则构建脚本会生成空 payload，仅适合源码编译验证，不适合发布。

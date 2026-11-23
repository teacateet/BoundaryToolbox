# 代码地图

这份文件按目录说明每个主要源文件的职责，方便新开发者快速定位修改点。

## 根目录

| 文件 | 作用 |
| --- | --- |
| `Cargo.toml` | crate 元数据、依赖、Windows API feature、构建依赖。 |
| `Cargo.lock` | 锁定依赖版本。发布前必须随版本号一起更新。 |
| `build.rs` | 编译 Slint、生成图标、打包内嵌 payload。 |
| `README.md` | 项目入口和文档导航。 |

## `src/main.rs`

程序入口。只负责连接 `app`、`core`、`vnt_platform` 三个模块，并启动应用。

## `src/app`

`app` 是 UI 控制器层。它持有 Slint 窗口、列表模型、偏好配置、日志文件和后台任务 channel。

| 文件 | 作用 |
| --- | --- |
| `mod.rs` | `AppController` 主结构、`AppMessage`、运行入口和共享 imports。 |
| `controller.rs` | 初始化 UI 状态、创建模型、绑定 Slint 回调、启动定时器。 |
| `actions.rs` | 安装、卸载、启动、诊断等按钮动作。 |
| `messages.rs` | 后台消息分发、日志追加、列表模型更新、UI 文本本地化。 |
| `dialogs.rs` | 应用内弹窗、确认框、路径输入框。 |
| `target.rs` | 自动/手动目标目录识别和路径校验入口。 |
| `drive_scan.rs` | 自定义盘符扫描。 |
| `diagnostics.rs` | 进程和端口诊断结果格式化。 |
| `system_checks.rs` | 系统环境检查 UI 适配。 |
| `servers.rs` | 社区服务器列表刷新和服务器地址复制。 |
| `server_list.rs` | 社区服接口请求、HTTP 响应解析、`ServerRow` 转换。 |
| `proxy_list.rs` | GitHub 代理列表获取、测速、行模型转换。 |
| `updates.rs` | 更新检查/下载按钮入口。 |
| `update.rs` | GitHub Release 查询、版本比较、自更新下载和替换助手。 |
| `font.rs` | UI 字体检查和自动安装入口。 |
| `logging.rs` | 日志过滤、启动时日志自动清理。 |
| `prefs.rs` | 用户偏好读写、语言和 VNT 配置迁移。 |
| `i18n.rs` | Rust 侧三语言文本和系统语言识别。 |
| `vnt_controller.rs` | VNT 页面回调、会话启动停止、状态同步。 |
| `vnt_rows.rs` | VNT 服务器和节点行模型转换。 |
| `background.rs` | 端口监控后台线程。 |
| `close_guard.rs` | 关闭窗口前检查运行进程并确认。 |
| `window.rs` | 窗口几何尺寸和高 DPI 适配。 |

## `src/core.rs`

核心业务公共类型和常量：

- 应用版本、Steam App ID、运行端口、下载 URL。
- `MANAGED_ITEMS`：安装器负责管理的文件/目录清单。
- `InstallState`、`ManagedRecord`、`InstallProgress` 等跨模块数据结构。
- `InstallerCore` 持有运行目录、安装器数据目录和日志回调。

## `src/core`

`core` 是所有高风险动作的边界，任何会改文件、杀进程、启动进程、下载依赖的逻辑都应放在这里。

| 文件 | 作用 |
| --- | --- |
| `installer.rs` | `InstallerCore::new()`、目录初始化、payload 标签。 |
| `install_ops.rs` | 安装、卸载、备份恢复、npm 依赖安装。 |
| `runtime_ops.rs` | PVP/PVE 启动、进程关闭、端口冲突处理。 |
| `payload.rs` | payload zip 读取、在线依赖下载、压缩包解压、哈希统计。 |
| `metadata.rs` | `.pve_installer` 路径、state/markers 读写。 |
| `filesystem.rs` | 复制、删除、原子写 JSON 等文件工具。 |
| `pathing.rs` | Boundary `Binaries\Win64` 目录校验。 |
| `process.rs` | 进程路径匹配、端口冲突、运行文件路径推导。 |
| `steam.rs` | Steam 在线状态和离线模式检测。 |
| `cleanup.rs` | 旧模组残留和冲突配置清理。 |
| `font.rs` | Maple Mono 字体下载和安装。 |
| `github_proxy.rs` | GitHub 代理测速和 URL 拼接。 |
| `system_checks.rs` | NVIDIA 驱动、防火墙等系统检查。 |
| `discovery.rs` | Steam/游戏目录发现。 |
| `util.rs` | 时间、隐藏命令、目录确保等小工具。 |

## `src/vnt_platform.rs`

封装 vendored VNT 核心：

- 构建 VNT 启动参数。
- 管理 VNT 会话生命周期。
- 转换 VNT server/peer 状态。
- 通过事件把状态送回 `app` 层。

## `ui`

Slint UI 文件只做声明式界面和回调，不承担业务逻辑。

| 文件 | 作用 |
| --- | --- |
| `appwindow.slint` | 主窗口、全局状态属性、页面切换、回调桥接。 |
| `models.slint` | Rust 和 Slint 共享的列表行结构。 |
| `header.slint` | 顶部导航和窗口头部。 |
| `i18n.slint` | Slint 侧三语言文本选择。 |
| `components/foundation.slint` | 最基础的文本组件。 |
| `components/surfaces.slint` | 面板、标题、状态块、进度块、详情行等展示容器。 |
| `components/controls.slint` | 按钮、导航 tab、开关、窗口控制按钮。 |
| `components/inputs.slint` | 输入框和 VNT 服务器下拉框。 |
| `components/overlays.slint` | 图片预览等全屏遮罩。 |
| `dialogs/app_dialog.slint` | 通用确认/错误/输入弹窗。 |
| `dialogs/drive_scan_dialog.slint` | 盘符扫描选择弹窗。 |
| `dialogs/install_progress_dialog.slint` | 安装/更新进度弹窗。 |
| `dialogs/proxy_select_dialog.slint` | GitHub 下载代理选择弹窗。 |
| `rows/diagnostics_rows.slint` | 诊断页端口行。 |
| `rows/server_rows.slint` | 社区服务器列表行。 |
| `rows/dialog_rows.slint` | 弹窗内部列表行。 |
| `rows/vnt_rows.slint` | VNT 服务器和节点行。 |
| `pages/start_page.slint` | 启动页。 |
| `pages/directory_page.slint` | 目录识别页。 |
| `pages/diagnostics_page.slint` | 诊断页。 |
| `pages/settings_page.slint` | 设置页。 |
| `pages/servers_page.slint` | 社区服务器列表页。 |
| `pages/vnt_page.slint` | VNT 联机页。 |

## `vendor/vnt`

本地并入的 VNT v2 源码。除非明确在做联机功能，不要在普通工具箱改动中修改这里。

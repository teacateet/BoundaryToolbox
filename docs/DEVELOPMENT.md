# 本地开发指南

## 环境要求

- Windows 10/11
- Rust stable，项目使用 edition 2024
- Visual Studio Build Tools 或等价 MSVC 工具链
- GitHub CLI 只在发布/PR 流程中需要

推荐开发目录结构：

```text
C:\BrtWork\SDK\rust-boundary-tool-box
C:\BrtWork\SDK\PVE一键启动包\边境
```

其中 `C:\BrtWork\SDK\PVE一键启动包\边境` 是本地私有载荷目录，正式构建时通过 `BOUNDARY_PAYLOAD_ROOT` 指向它。

## 构建模式

### 源码验证构建

不设置 `BOUNDARY_PAYLOAD_ROOT` 时也能编译，但内嵌 payload 为空。这个模式只适合检查代码是否能编译，不适合安装测试或发布。

```powershell
cargo check
cargo test --all-targets
```

### 带 payload 的本地构建

```powershell
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'
cargo build
```

### 发布构建

```powershell
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'
cargo build --release
```

正式发布前必须确认构建日志里没有：

```text
payload root not found; building source-only executable with empty embedded payload
```

## 必跑检查

每次改动至少跑：

```powershell
cargo fmt --check
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo test --all-targets
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo clippy --all-targets -- -D warnings
```

涉及发布或安装流程时再跑：

```powershell
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo build --release
```

## 开发约束

代码风格和模块边界详见 [STYLE.md](STYLE.md)。本节只列最容易影响稳定性的约束。

### UI 回调不能阻塞

Slint 回调只做轻量状态切换和后台任务启动。下面这些操作必须放到后台线程：

- 网络请求
- 文件复制/删除/解压
- npm 安装
- 进程扫描、结束进程
- GitHub 代理测速

推荐模式：

```text
callback -> AppController 方法 -> thread::spawn -> core 方法 -> AppMessage -> drain_messages
```

### 业务逻辑不要写进 Slint

Slint 文件只放：

- 属性绑定
- 布局
- 视觉状态
- 回调转发

业务判断应放在 Rust 层。复杂数据转换优先放在 `src/app/*_rows.rs` 或 `src/app/messages.rs`。

### 高风险操作只能在 core 层

下面这些行为必须通过 `InstallerCore` 或 `core` 子模块完成：

- 写入游戏目录
- 删除文件
- 备份和恢复
- 启动游戏或服务
- 结束进程
- 读取 Steam 状态
- 下载并校验外部依赖

这样 UI 层不会绕过统一校验。

## 新功能放哪

常见修改点：

- 新按钮或页面：先改 `ui/pages/*` 和 `ui/appwindow.slint`，再在 `src/app/controller.rs` 绑定回调。
- 新后台动作：在 `src/app/actions.rs` 或独立 app 模块里启动线程，在 `AppMessage` 中增加结果类型。
- 新安装文件：更新 `src/core.rs::MANAGED_ITEMS`，并明确它是内嵌 payload、ProjectRebound 在线文件，还是独立在线依赖。
- 新诊断项：核心检查放 `src/core/system_checks.rs`，UI 文案放 `src/app/system_checks.rs`。
- 新持久化配置：更新 `src/app/prefs.rs`，注意旧配置迁移。
- 新列表行：更新 `ui/models.slint`、对应的 `ui/rows/*_rows.slint` 和 Rust 行转换函数。

## 日志位置

运行时日志写入：

```text
<工具箱所在目录>\installer_tool\logs\installer_YYYYMMDD_HHMMSS.log
```

启动、安装、卸载、更新失败都先看最新日志。日志会在启动时自动清理过旧或过大的文件。

## 版本号

版本号有三处需要一致：

- `Cargo.toml`
- `Cargo.lock`
- `src/core.rs::APP_VERSION`

改完 `Cargo.toml` 后运行一次：

```powershell
cargo update -p boundary_toolbox --precise <新版本号>
```

## UI 文案

Rust 侧文案通过 `src/app/i18n.rs` 或 `messages.rs` 做三语言处理。Slint 侧文案通过 `ui/i18n.slint` 的 `I18n.tr()`。

新增用户可见文本时，至少补齐中文、英文、日文三个参数。暂时没有准确日文时也要给可理解的占位翻译，避免空白 UI。

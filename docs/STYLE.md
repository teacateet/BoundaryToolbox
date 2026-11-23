# 代码整理和维护规范

这份文档约束日常改动的代码风格和模块边界。目标不是追求花哨抽象，而是让安装器这种会改文件、起进程、下依赖的桌面工具稳定、可审查、可回滚。

## Rust 代码

- 生产代码不要使用裸 `unwrap()` / `expect()`。能向上传递的错误用 `anyhow::Result` 和 `Context`，能降级的分支用 `if let`、`match` 或 `Option` 组合子。
- 不为“过编译”无脑 `.clone()`。允许克隆的常见场景是 `Arc`、channel sender、Slint `SharedString`、跨线程闭包捕获和小型 UI 行数据；大对象优先借用或拆出只读引用。
- 文件系统、网络、进程和压缩包操作必须落在 `src/core` 或明确的后台任务里，UI 层只调度。
- 错误信息要写给维护者和用户都能理解，包含关键路径、下载地址、进程名或文件名。
- 测试里也尽量避免 `unwrap/expect`。断言 `Option`/`Result` 时优先比较 `Some(...)`、返回 `Result<()>`，或显式匹配错误。

## Slint UI

- `.slint` 文件只负责布局、属性绑定、视觉状态和回调转发，不承载安装、下载、启动、校验等业务流程。
- 回调函数必须保持轻量。耗时操作通过 `thread::spawn` 进入后台，再用 `AppMessage` 回到 UI 线程刷新状态。
- Rust 侧更新列表模型时优先原地修改已有 `VecModel`，避免替换模型后打断 Slint `ListView` 绑定。
- 页面文件的导入保持精确，避免复制整套组件列表后长期无人维护。
- 用户可见文案必须补齐中文、英文、日文三个参数。

## Payload 和在线依赖

- `MANAGED_ITEMS` 描述工具箱最终管理的目标文件，不等于“全部内嵌 payload”。
- `build.rs` 只内嵌本地必需载荷。ProjectRebound、BoundaryMetaServer、Node.js 都是安装时在线获取，不要放进内嵌 zip。
- 新增安装项时必须同时更新分类函数、安装记录、卸载逻辑和文档说明。
- 发布前必须带 `BOUNDARY_PAYLOAD_ROOT` 构建，并确认没有空 payload 警告。

## 日志和诊断

- 新增日志时优先记录动作、目标路径和结果，不记录大块 stdout/stderr。
- 外部命令输出必须裁剪后写入日志，避免再次出现超大日志文件。
- 诊断页展示的信息来自核心模块的结构化结果，避免 UI 直接扫描系统状态。

## 提交前检查

```powershell
cargo fmt --check
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo test --all-targets
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo clippy --all-targets -- -D warnings
```

涉及安装、更新、payload 或启动流程时，再跑发布构建：

```powershell
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo build --release
```

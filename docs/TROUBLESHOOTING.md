# 常见问题排查

## 先看日志

多数问题都能从会话日志定位：

```text
<工具箱所在目录>\installer_tool\logs\installer_YYYYMMDD_HHMMSS.log
```

日志会记录：

- 运行目录
- 载荷目录
- 安装器目录
- 管理员状态
- 下载 URL
- 安装/卸载每个文件的来源和目标
- 启动进程路径
- 失败原因

## 安装时报“内嵌载荷缺少”

可能原因：

- 发布构建时没有设置 `BOUNDARY_PAYLOAD_ROOT`。
- `build.rs::MANAGED_ITEMS` 包含了本地 payload 目录没有的在线依赖。
- 本地 payload 目录缺少 `DT_ItemType.json`、`dxgi.dll` 或 `steam_appid.txt`。

处理：

1. 确认构建命令带 `BOUNDARY_PAYLOAD_ROOT`。
2. 确认构建日志没有空 payload 警告。
3. 确认 `build.rs::MANAGED_ITEMS` 只包含真正内嵌的文件。

## 点击启动失败

启动分 PVP 和 PVE。

PVP 必需：

- `BoundaryMetaServer-main`
- `BoundaryMetaServer-main\index.js`
- `BoundaryMetaServer-main\node_modules\express`
- `BoundaryMetaServer-main\node_modules\body-parser`
- `BoundaryMetaServer-main\node_modules\protobufjs`
- `nodejs\node.exe`
- `ProjectBoundarySteam-Win64-Shipping.exe`

PVE 额外必需：

- `ServerLauncher\ProjectReboundServerLauncher.exe`
- `ServerLauncher\slint_cpp.dll`

常见原因：

- 用户只更新了工具箱 exe，没有重新点“安装/更新”。
- 安装中途取消，导致依赖目录不完整。
- BoundaryMetaServer 被错误归类，跳过了依赖安装。
- Steam 未运行或处于离线模式。
- 必需端口被其他进程占用。

处理顺序：

1. 使用最新版工具箱。
2. 重新执行“安装/更新”。
3. 查看日志里是否出现：
   - `安装在线 BoundaryMetaServer`
   - `安装登录服务器依赖`
   - `安装在线 Node.js 运行时`
   - `ProjectRebound 文件已解压`
4. 再点击启动。

## PVE 启动缺 ServerLauncher

PVE 必须依赖 ProjectRebound Release.zip 中的 `ServerLauncher`。如果缺失：

- 检查 ProjectRebound 下载是否成功。
- 检查 Release.zip 结构是否变化。
- 检查 `PROJECT_REBOUND_ONLINE_FILES` 是否包含 `ServerLauncher`。
- 重新安装。

## 登录服务器启动后立即退出

常见原因：

- `node_modules` 缺失。
- `npm ci` 失败。
- 8000/9000 等端口被占用。
- `BoundaryMetaServer-main` 目录被手动替换。

处理：

1. 执行“诊断”查看端口。
2. 关闭占用进程。
3. 重新安装。
4. 如果仍失败，进入 `BoundaryMetaServer-main` 手动执行：

```powershell
..\nodejs\node.exe index.js
```

终端输出通常会给出直接错误。

## 自动更新找不到资产

自动更新优先查找 GitHub Release 里的：

```text
boundary_toolbox.exe
```

如果 Release 只上传了中文名 exe 或其它名称，更新逻辑会退回到第一个 `.exe`，可能下载错文件。

处理：

- Release 资产统一命名为 `boundary_toolbox.exe`。
- 不上传 PowerShell 脚本作为 Release 资产。

## GitHub 下载慢或失败

工具箱支持 GitHub 代理。相关代码：

- `src/core/github_proxy.rs`
- `src/app/proxy_list.rs`
- `src/app/update.rs`

排查：

1. 在设置页刷新代理列表。
2. 选择延迟最低且可用的代理。
3. 查看日志中的实际下载 URL。
4. 必要时清空代理前缀直接访问 GitHub。

## VNT 联机启动失败

优先看：

- 网络编号是否为空。
- VNT 服务器地址是否有效。
- 是否已有旧 VNT 会话未关闭。
- 防火墙是否拦截 UDP/TUN。

相关代码：

- `src/vnt_platform.rs`
- `src/app/vnt_controller.rs`
- `src/app/vnt_rows.rs`

## 日志文件过大

启动时会自动清理：

- 超过保留天数的日志。
- 单个过大的 `.log`。
- 总大小超过上限的旧日志。

相关代码：

- `src/app/logging.rs`
- `src/app/messages.rs`

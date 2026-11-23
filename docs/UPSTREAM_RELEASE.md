# 基底工具箱的历史发布流程

此文件保留原工程的发布记录。BRT 使用独立的[测试包流程](RELEASE.md)。

本文记录发布新版本的推荐步骤。默认发布 Windows exe 到 GitHub Releases。

## 1. 确认发布范围

发布前先确认：

- `origin/main` 已包含需要发布的 PR。
- 工作树没有无关改动。
- 没有把本地 exe、日志、PowerShell 脚本、临时文件加入 Git。

```powershell
git fetch origin --prune --tags
git status --short --branch
git log --oneline --decorate -5 origin/main
```

## 2. 更新版本号

需要同步更新：

- `Cargo.toml`
- `Cargo.lock`
- `src/core.rs::APP_VERSION`

示例：

```powershell
# 修改 Cargo.toml 和 src/core.rs 后
cargo update -p boundary_toolbox --precise 19.20.6
```

## 3. 检查 payload 分类

发布前确认 `build.rs` 只打包本地内嵌文件：

- `DT_ItemType.json`
- `dxgi.dll`
- `steam_appid.txt`

不要把下面这些在线依赖放进 `build.rs::MANAGED_ITEMS`：

- `Payload.dll`
- `ServerLauncher`
- `project_rebound_version.txt`
- `BoundaryMetaServer-main`
- `nodejs`

它们的安装逻辑分别在 `src/core/payload.rs` 和 `src/core/install_ops.rs`。

## 4. 必跑验证

```powershell
cargo fmt --check
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo test --all-targets
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'; cargo clippy --all-targets -- -D warnings
```

如果出现 payload root not found 警告，不能继续发布。

## 5. 编译 Release

```powershell
$env:BOUNDARY_PAYLOAD_ROOT='C:\BrtWork\SDK\PVE一键启动包\边境'
cargo build --release
```

生成文件：

```text
target\release\boundary_toolbox.exe
```

复制一份带版本号的本地留档：

```powershell
Copy-Item target\release\boundary_toolbox.exe `
  'C:\BrtWork\SDK\PVE一键启动包\边境社区服工具箱-Rust-v<版本>.exe' -Force
```

发布到 GitHub 的资产文件名保持：

```text
boundary_toolbox.exe
```

自动更新逻辑优先找这个名称。

## 6. 提交和推送

```powershell
git add -- Cargo.toml Cargo.lock src/core.rs build.rs src/core/*.rs src/app/*.rs ui/*.slint ui/pages/*.slint docs README.md
git commit -m "Prepare v<版本> release"
git push origin HEAD:main
```

如果只是修复发布问题，提交信息可以更具体，例如：

```text
Fix login server install classification
```

## 7. 打 tag

```powershell
git tag -a v<版本> -m "v<版本>"
git push origin v<版本>
```

确认 tag 指向当前 main：

```powershell
git rev-list -n 1 v<版本>
git rev-parse origin/main
```

两个提交 hash 应一致。

## 8. 创建 Release

```powershell
gh release create v<版本> `
  'C:\BrtWork\SDK\PVE一键启动包\boundary_toolbox.exe' `
  --title "v<版本>" `
  --notes-file release-notes.md `
  --latest
```

不要上传 PowerShell 脚本。

## 9. 核对 Release

```powershell
gh release view v<版本> --json tagName,name,isDraft,isPrerelease,publishedAt,targetCommitish,url,assets
Get-FileHash 'C:\BrtWork\SDK\PVE一键启动包\boundary_toolbox.exe' -Algorithm SHA256
```

确认：

- Release 不是 draft。
- 资产只有预期 exe。
- 资产名是 `boundary_toolbox.exe`。
- GitHub 显示的 sha256 和本地一致。

## 10. 发布后烟测

至少做一次：

1. 启动 exe。
2. 检查 UI 显示版本号。
3. 执行“安装/更新”。
4. 查看日志确认 BoundaryMetaServer、Node.js、ProjectRebound 都走了正确分支。
5. 点击 PVP/PVE 启动。
6. 检查自动更新能看到最新 Release。

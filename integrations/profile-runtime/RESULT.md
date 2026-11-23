# 本机档案运行目录

启动器以所选安装的 `LaunchFiles.server_dir` 为识别来源，将核验后的服务、Node 模块、模式定义和协议发布到 `<installer_home>/profile-runtime/apps/<SHA-256>/`。HTTP 默认绑定 `127.0.0.1:8000`，原生 RPC 绑定 `127.0.0.1:6969`，匹配 TCP 和 UDP 均绑定 `127.0.0.1:9000`。原有持久化版本与托管副本读取同一份档案；没有存储层的官方独立 main 使用相同的本地目录约定。`profile-location.json` 记录来源、版本类别和实际档案目录。[兼容目录及边界](README.md) · [Rust 接口](../../src/core/local_profile.rs) · [应用准备](prepare.cjs)

## 2026-11-21 新安装验收

### 原生人机匹配的生产接入

生产 `local_profile::KIT` 已注册并由 `prepare.cjs` 依次组合两组补丁。包装脚本已有的 `components/profile-runtime/prepare.cjs` 必须取新版；在既有清单上新增以下 **4** 项（源文件均在 `integrations/native-menu/`）。`menu-trace.cjs` 只做离线日志诊断，可选；`verify*.cjs` 不参与运行。

```text
components/native-menu/session-progress.patch
components/native-menu/session-progress.cjs
components/native-menu/ready-room.patch
components/native-menu/ready-room.cjs
```

写入方使用 [running_pve_room_lease](../../src/core/local_profile.rs) 从指定 home、`LaunchFiles` 和实际监听者得到唯一路径；Rust 的返回值与托管 Node 的 `roomFileFor(__dirname)` 在三个来源的每次运行中一致。准备时校验整个应用目录摘要；运行中重验入口、存储、位置、所选来源入口/存储及新增 `menu_sha256`（三个实际模块、模式表、匹配/队伍/列表的协议）。三个模块还必须与当前 EXE 的 KIT 原字节相等。旧 receipt、缺失 receipt、错误目标、异进程、应用组件漂移均不能给房间管理器授予路径。升级不迁移、不扫描旧玩家档案，也不将 lease 写入应用或档案目录。

| 隔离验证 | 结果 |
| --- | --- |
| V0.8.4 发布包、独立 main、本机基线 | 每来源各启停两个真实 Node 服务进程；使用真实 HTTP 登录、gate 绑定和该来源生成的 protobuf，执行大厅查询、错难度、停止后重选、房间上线/撤销、`SetPresence`。新进程即使能看到同一位置的 lease，启动/重登不自动分配，须重新点击。 |
| 未启用与存档 | 无记录、旧记录过期、正常房间选择及停止前后，合成旧档案逐文件摘要不变。菜单账号不创建存档；同安装的原生军械写入可通过真正重启后的三种角色大小写读回；其他玩家和源应用未变。未启用时的原匹配器另在发布包 HTTP A/B 里回归。 |
| 协议边界 | 真实路由是 `StartUnityMatchmaking`、`QueryUnityMatchmaking`、`StopUnityMatchmaking`。开始内层 `0800`，查询/停止内层 0 字节；没有增加 ticket、地址或所谓 Restart RPC。服务端选中房间、合成客户端上报 `InMatch` 均不代表零售进图。 |
| 复现 | `node integrations/profile-runtime/verify.cjs`，Node 24.14.0 / 24.14.1 均通过。运行中的 Rust 只读路径测试由该脚本按实际获取的随机端口调用。原生菜单独立套件 21/21。只使用回环临时端口；未触碰本机玩家档案或游戏、未运行打包脚本。 |
| Release | 独立目录 `cargo build --release --offline --locked --bin BRT --target-dir integrations/profile-runtime/.build/launcher-084` 成功。`node integrations/profile-runtime/verify-menu.cjs --embedded integrations/profile-runtime/.build/launcher-084/release/BRT.exe` 对准备脚本和六个菜单文件逐字节比对 **7/7**。这一测试构建 SHA-256：`0e9bcee1d5d45e1534879e285a3e704d040438ab5066d248cf0f7efb4a50b163`，不是最终玩家 ZIP 的摘要。 |

[生产组合器](prepare.cjs) · [重启与原生协议回归](verify-menu.cjs) · [房间规范与真机待证](../native-menu/RESULT.md)

### 来源依据

| 来源与凭据 | 已确认的差异与处理 |
| --- | --- |
| [ProjectRebound V0.8.4](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4) | 本地 Release.zip 为 12,469,495 字节，SHA-256 `fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094`，与官方发布资产摘要一致。入口为 `14ae33e2731895279f39fdfaf6295cd59bc2e2a8deea780e166dd5665d01ad89`，存储为 `69864e5d193f06fcc1054220f4d1a0bc1170bf040d9ed2026fe34b562148cde1`。发布版原入口指定外部匹配地址，读档 protobuf 与既有本机版并非同一布局。 |
| [BoundaryMetaServer main，007850d0](https://github.com/STanJK/BoundaryMetaServer/tree/007850d0fd07c688d0a1ec1ff2a8771726c3463c) | 本地只读检出的提交与检查时远端 main 一致。LF 入口为 `de98c06ce2ba56d4c35312d634883cce99df7b659ccca4bbef49b4f748c60f9a`。原树有 23 份协议，缺少存储与定义索引模块，两个更新 RPC 只回执。对这一版验证自身协议、定义、依赖后，使用随工具箱的 V0.8.4 档案源码。没有使用游戏安装备份或用户数据作为输入。 |
| 本机修订档案基线 | 入口 `3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d`，存储 `c85d1cc7a0426b8c42a22ef4c6fe60562ce58e3fdb3c41debebbd987bfae866e`。V0.8.4 差异涉及这两个文件及 3 份协议，应用后重验完整 JS 摘要及 41 份协议树 `325f85f9b74982cf8817cd5c4b2a4c8e6f3ee6aa5ef3f300d62f727e823b7538`，才接入同一后续部署流程。 |

独立源码包由 3 份 JS 和 41 份 protobuf 生成，文件清单摘要为 `c55dd8e4995215590fb6557d98e5e7637c282f4daa2abaf6de5031ad4dce67b0`。不含玩家 JSON、依赖或可执行文件。原版差异、生成器及源许可证均在本目录。[生成器](pack-release.cjs) · [源码差异](release-084.patch)

- 使用 Windows 合成安装和临时回环端口，经生产 Rust `prepare` 启动 Node。实测两套新安装均在只读登录时不创建档案，首次 RPC 写入才在各自 `data/loadouts` 建立档案。两枪、两个槽位 9 配件和左右挂载经独立进程重启，分别以 `Peace`、`PEACE`、`pEaCe` 登录会话内查询，所得六项相同。PvE 三档、PvP 的 Purge、区域 ID 和发现地址均通过真实回包断言。
- 首次回归发现原生读回正确时 REST 单角色读取仍返回默认主武器。已在工具箱托管副本的 REST 读取及整份配装合并复用同一角色键规则。用第三种拼写再经 HTTP 写入不产生第二个键。有两个已有精确键时逐个读回不同结果，模糊拼写只返回默认值，未知角色 HTTP 404。只读会话后的档案、旁侧合成玩家文件和源应用摘要保持不变。[REST 补丁](rest-role-key.patch) · [回归](verify.cjs)
- 使用随附 Node `24.14.0` 验证独立 main 的 LF 安装，用 Node `24.14.1` 验证同提交的 CRLF 工作树。两次均通过 2 项 Rust 单测及整组 Node 回归，亦覆盖旧版及其预装组合。未知协议、入口/存储混搭被拒绝。网络共享路径由纯路径单测覆盖，未挂载真实网络盘或传输玩家数据。
- 独立目录完成 `cargo build --release --offline --locked`。产物为 `.build/launcher-084/release/BRT.exe`，SHA-256 `64281673b624fb3fbd2e63addb1d14ab5e89530fecd42e396224016fda78b8c9`。已从 PE 确认准备脚本、两个新增补丁、V0.8.4 源码包及许可证均在该构建中。未替换主线运行中的 EXE，未启动游戏、未做图形客户端内点击或远程组队验收。

## 启动与复用

```rust
pub(crate) fn prepare(installer_home: &Path, files: &LaunchFiles) -> Result<ProfilePlan>;
pub(crate) fn running_managed_pid(installer_home: &Path, files: &LaunchFiles) -> Result<Option<u32>>;
```

启动器在已有的启动锁内查询 `running_managed_pid`。返回 `Some(pid)` 时可复用进程，并按该 PID 过滤 HTTP、RPC 和两项匹配端口。返回 `None` 时继续现有端口冲突检查。需要由启动器发送写请求的调用方可在每次发送前核对本次会话的 PID。玩家从游戏内军械入口发起的操作走角色和配件 RPC。读取 Windows 监听表失败通过 `Err` 报告。

```rust
let files = crate::core::process::launch_files(selected_win64);
let current = local_profile::running_managed_pid(&self.installer_home, &files)?;
// 在启动分支内构造命令，继续使用主线的日志句柄、子进程和就绪检查。
match local_profile::prepare(&self.installer_home, &files)? {
    local_profile::ProfilePlan::Managed(profile) => {
        self.log(format!("档案目录 {}", profile.loadout_dir.display()));
        let child = profile.command().stdout(stdout).stderr(stderr).spawn()?;
        // 就绪时再次确认查询结果等于 Some(child.id())。
    }
    local_profile::ProfilePlan::Unsupported { reason } => {
        // 向用户显示版本原因，普通启动继续使用所选安装提供的档案服务。
    }
}
```

`ManagedProfile.app_dir`、`ManagedProfile.loadout_dir` 均为绝对路径。`command() -> std::process::Command` 使用所选 Node、应用自己的工作目录和绝对入口，参数为 `--no-global-search-paths <app_dir>/index.js`。子进程仅继承必要的 Windows 环境变量，并设置 `NO_PROXY=*`。包由应用内的标准 CommonJS `node_modules` 解析。

## 识别与组合

- `prepare` 先校验原始 `index.js`、`loadoutStore.js`、定义索引、协议、游戏定义及随附的 Node 依赖。在工具箱的临时副本中按下表组合，最后设置回环监听及实际档案位置。预装输入按依赖的逆序还原，并与原始 SHA-256 比对。已安装配件投影的输入使用原部署器保留的备份做同样的校验。
- 两个并发准备进程各自构建临时目录，按内容摘要发布并核对最终文件树。已有应用内容漂移、缺失依赖和 I/O 错误通过 `Err` 报告。未识别的版本返回 `Unsupported { reason }`。已识别版本的发布凭据按工具箱资源版本、所选 Node 和源目录区分。
- PID 查询读取系统端口表，要求 `127.0.0.1:8000` 的监听者唯一。它核对所选可执行文件、三个实际命令行元素、工作目录、已发布应用路径、入口与存储模块指纹，以及 `profile-location.json` 对应的安装和真实存档位置。返回前再次查询监听者。热路径只读小文件和候选 PID，两个独立启动器进程可使用同一份上次全量验证的凭据。
- 玩家目录按原服务的 `data/loadouts` 解析。已有目录连接指向其实际目标，新安装由原存储层在首次写入时创建目录。玩家 JSON 的读取、写入、合并以及临时文件替换仍由同一个服务存储模块处理。

| 顺序 | 组件 | 用途 |
| --- | --- | --- |
| 1 | `loadout/deploy.cjs` | 生成原生角色与武器配件的档案回包 |
| 2 | `native-menu/playlist.patch` | 接入本机列表和区域查询 |
| 3 | `match-ui/role-read-case.patch` | 同一已知角色可按唯一的大小写别名读取 |
| 4 | `loadout/native-role-write.patch` | 角色和配件写入复用相同的已存键。已有多个键时保留各自的精确匹配 |
| 5 | `profile-runtime/rest-role-key.patch` | REST 和原生 RPC 保持相同的角色键处理 |
| 6 | `native-menu/session-progress.patch` + `session-progress.cjs` | 核对登录人、菜单选择及回环匹配器的真实状态 |
| 7 | `native-menu/ready-room.patch` + `ready-room.cjs` | 只在目标托管应用存在本机房间记录时，按原生难度、地图和模式类匹配 |

Locke 的对照回归在旧版记录到 `Sniper`、`SNIPER` 两个键，角色挂载与配件分落两份记录。补丁版在两种写入顺序下，读取两种拼写都得到同一套挂载与配件。纳入 Rust 发布包的补丁与该回归记录的 SHA-256 相同，为 `2744eb46d704a625ea901e23703aab5400c5f7ed2d4c16967007446032953703`。[Locke 的原生对照记录](../loadout/native-lobby-proof.json) · [补丁](../loadout/native-role-write.patch)

## 隔离回归

Windows、Rust/Cargo、本地依赖缓存和 Git 可用于复验。在本目录执行 `node verify.cjs`。测试先离线编译生产 `local_profile.rs` 和实际的隐藏进程函数，再从该 Rust 接口启动 Node。Git 仅用于独立构造预装补丁的输入样本。输入可通过 `PROFILE_TEST_SOURCE`、`PROFILE_TEST_MODULES`、`PROFILE_TEST_NODE` 指定。[回归入口](verify.cjs) · [Rust 探针](regression/probe.rs) · [测试依赖](regression/Cargo.toml) · [依赖锁](regression/Cargo.lock)

| 场景 | 实际结果 |
| --- | --- |
| 并发首次准备 | 两个独立进程返回相同发布目录，最终只有一份应用 |
| 监听者识别 | 独立进程查到正确 PID。外来监听、端口释放、更换 home、安装或 Node、源码与位置文件变动、凭据缺失均返回 `None`。通配监听、IPv6 通配和多 PID 由系统表样本单测覆盖 |
| 四项改动同进程 | 系统表记录 3 个 TCP 和 1 个 UDP 回环监听。实际 HTTP 会话及原生 RPC 往返，读到三档 PvE、有效区域和大小写别名角色。HTTP 部件写入可由原生 RPC 读回，原生角色槽位修改可由 HTTP 读回 |
| 游戏内军械的原生写入与读取 | 独立合成账号通过正常会话，交替用 `Peace`、`PEACE` 调用 `UpdateRoleArchiveV2` 和 `UpdateWeaponArchiveV2`。进程重启并重新登录后，分别以 `pEaCe`、`PEACE`、`Peace` 调用 `GetPlayerArchiveV2`，读回相同六项，磁盘只有首个角色键 `Peace`。重启后的只读请求保持档案字节一致 |
| 存档与重启 | 对接已有合成目录连接。重启后读回相同内容，只读请求前后档案字节相同，另一玩家的 JSON 和来源应用逐文件摘要相同 |
| 失败与恢复 | 未知入口及变动定义树返回版本原因。损坏的已发布应用返回错误，恢复核验后继续运行 |
| 已预装组合 | 分别用原部署器和 Git 构造三项、四项已预装的输入，均发布到同一已核验的目录，两个合成玩家档案的字节一致 |
| 首次存档位置 | 新建的第二个合成安装识别出独立的首存目录，准备时该目录仍待首次保存创建 |

同一原生军械用例先在三项组合复现了重启后返回默认弹匣的问题。加入第四项后，两次独立回归都从真实 protobuf 回包读到以下值。

| 原生档案字段 | 重启后实读 |
| --- | --- |
| 主武器 / 副武器 | `PEACE_GSW-AR` / `PEACE_RU-AKM` |
| 两枪的弹匣（槽位 9） | `GSW-AR_MAG-AR-EXT` / `RU-AKM_MAG-AK-EXT` |
| 左挂载 / 右挂载 | `PEACE_TAC-EMP` / `PEACE_ATK-HE` |

早期两次分别选用 Node `24.14.1` 和随附的 `24.14.0`，并使用已识别的本地依赖。早期固定端口试验曾核对自有 PID、另一安装和关闭后的结果；当前回归不再提供固定端口选项，即使环境中保留旧开关也只使用随机回环端口。测试结束时回收各次服务和监听。图形客户端中从军械入口点击、保存、重新进入查看的验收仍待实机执行。

旧基线的四项改动继续直接嵌入；本次新增的发布源码、差异补丁、REST 补丁和许可也由同一套 Rust 注册。后续发布应同时保留本目录中的这些资源。[Rust 发布与识别](../../src/core/local_profile.rs)

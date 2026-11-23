# 原作菜单里的 PvE 和新手教程

2026-11-21 补验：[单人派对与实物地区表](SOLO-PARTY.md)。三种托管来源已纠正单人成员身份及状态隔离，预览地区名称命中零售表；游戏内队长提示和弹窗可见行数仍待验收。

## 2026-11-21 大厅内选择本机房间

**状态：已验证原生请求到服务端房间选择，尚未验证零售客户端收到地址或进图。** 本次不是再增三行列表，也不改变开服后只进大厅的启动方式。加载大厅、读取列表和上报 `InHall` 都不会分配房间。只有同一 RPC 连接正常绑定账号并发送发布包定义的 `StartUnityMatchmaking`，其中请求人匹配且 `GameMode` 为原表的 PvE 行名或匹配字符串，才核对本机房间。

**配置中有一处可复现的不匹配。** V0.8.4 的 `ServerLauncher/serverconfig.json` 随包是 `mode=pve, offline=true, difficulty=normal, map=Warehouse, port=7777`；同包 `BoundaryMetaServer-main/game/definitions/DT_GameModeDefinition.json` 的 `Rush_PVE_Normal.MapsForMatch` 只有 `MiniFarm / CircularX / OSS`。`Warehouse` 在简单人机里，另有 `DataCenter`；困难人机是 `OSS / CircularX`。所以普通人机点击不能把这个默认房间报告成原表认可的匹配。第一次真机验收可在已有房间配置界面选择 **普通 + CircularX**；要保留 Warehouse 则选择 **简单 + Warehouse**，并从大厅点相同难度。本次未改任何用户配置，也未把这一处差异推断成所有进局失败的原因。[发行包](../../.workflow/qa/rebound-084/Release.zip) · [地图与模式回归](ready-room.test.cjs) · ServerLauncher 的实际模式类

**生产接入**：三类已支持的来源现已由 [profile-runtime](../profile-runtime/RESULT.md) 自动接入。在已定向回环的托管入口上，保留原来的 `playlist.patch`、`native-menu.cjs`，依次应用 `session-progress.patch`、[ready-room.patch](ready-room.patch)，并将 `session-progress.cjs`、[ready-room.cjs](ready-room.cjs) 放在该入口同级。房间管理器通过 `local_profile::running_pve_room_lease(installer_home, files)` 获取本次实际托管应用的路径；不需枚举目录，也不要手算摘要。菜单模块不监听端口、不启客户端，也不向客户端主动推送。下面的**工具箱内部记录**不是原版 RPC 或游戏协议：

| 写入方：本机房间管理器 | 约定 |
| --- | --- |
| 位置 | 托管目录为 `<home>/profile-runtime/apps/<64位应用摘要>` 时，记录写在 `<home>/profile-runtime/pve-ready-<同一摘要>.json`。可由 `roomFileFor(appDir)` 得到；不是玩家档案、游戏目录或应用源文件。 |
| 何时写 | 本次进程的原生日志确认地图、模式类、端口和开始监听，且游戏子进程存活并持有该 UDP 端口后，才原子替换 UTF-8 JSON。每次启动使用新的 `runId`；例如每 5 秒复查存活和端口归属再续期。就绪依据可复用[本机启动检查](../../src/core/runtime_ops/local_pve.rs:297)，不能仅从静态配置或任意进程占端口发布。 |
| 字段 | `v: 1`；`runId` 为本次启动标识（1 至 96 位 ASCII 字母、数字、下划线或连字符）；`expiresAt` 为 Unix 毫秒整数，距当前最多 15 秒；`mode: "pve"`，`offline: true`；`difficulty` 为小写；`map`、`gameClass`、数值 `port` 取实际就绪值。只返回 `127.0.0.1`，不接受自报目标地址。 |
| 停止或回退 | 已选房间每次查询重验；记录缺失、过期、错图或换进程均回到排队，不保留旧的 found。旧票据不能改指另一地图或端口。保持本地模式但未就绪/已停时可写 JSON `null` 阻止**新**选择回退；新进程就绪后重新点击。彻底取消接入时删除记录，新选择恢复原回环匹配器，已选会话不会转交。 |

**取证**：运行 `node menu-trace.cjs --sessions <本次单进程档案服务日志>`。同一次原生开始的序号下，`room ready map=CircularX` 指本机房间符合定义；`room wait=mode-map` 指地图不在该难度；`client presence=InMatch` 仅来自该连接实际发来的 `SetPartyPresenceRequest.Presence`。不读全局 `QueryPresence` 来推断，不输出账号、票据、地址或原始正文。发布包的开始回包仍是 `08 00`，查询正文仍是 **0 字节**。原版 UI 的 `On Player Join Game` 无参数，虽可见 `HasLobbyStarted` 返回值，但这些导出没有给出客户端应从哪里取得地址。正常 Presence 枚举 · 匹配完成 UI

**离线回归**：在本目录运行 `node --test verify.test.cjs release-menu.test.cjs session-progress.test.cjs ready-room.test.cjs`，系统 Node 24.14.1 与随附 24.14.0 各 **21/21**；`099d3f68…` 主线暂存入口的两个补丁逐段唯一上下文另过 **1/1**。本机 `3961c933…` 入口在内存中完整应用并通过语法校验；`14ae33e2…` 发布入口在临时副本验证补丁正向应用、可逆检查、实际 HTTP 登录与绑定 RPC。只启回环临时端口与合成 UDP 房间，不启游戏。大厅未选不分配，点击后找到，另一连接不能代报状态，撤销就绪后返回排队，本机房间路径的下游 HTTP 请求数为 **0**；未启用本机房间时仍通过原匹配器 A/B。[发布包实测](release-menu.test.cjs) · [脱敏取证](menu-trace.cjs)

**真机验收仍缺**：同一次点击后客户端接收到的正常分配入口及地址字段，随后客户端的目标地图载入或本次游戏服接受该玩家的记录。合成的 `InMatch`、服务端的 `found`、单纯地图服务已监听，均不能替代这些证据。

## 2026-11-21 选择 PvE 后的查询

本轮只处理点击选择后的排队/查询流程，不改三行播放列表。V0.8.4 发布包的 `StartMatchmakingRequest` 定义了 `Payload = 1` 和 `GameMode = 2`；开始响应只有 `StatusCode = 1`，不回传本机匹配器票据。`QueryUnityMatchmakingRes` 没有定义字段。普通游戏菜单属性中，`UMG_Assignment.K2_OnMatchmakingStatusChange` 的参数 `InMatchmakingStatusd` 明确属于客户端枚举 `EPBMatchmakingStatus`；此绑定不说明哪条网络消息驱动枚举，更不能把匹配器的 `found` 编码为客户端的 `Complete`。原始菜单属性 · [发布包与绑定测试](session-progress.test.cjs)

**可复现的阻断**：在正常 HTTP 登录并绑定 RPC 的合成会话中，选择原表中的 `NORMAL BOT`。以原列表补丁为基线，下游确认 `queued` 后，三次发送空查询都没有向匹配器发出状态 GET。原开始回包内层为 `08 00`，查询内层都是 0 字节。旧测试中显式传入的票据取自测试自己的匹配器，并非游戏从开始回包读到的值。

| 同一 V0.8.4 普通会话，独立回环匹配器 | 仅原列表补丁 | 增加会话补丁 |
| --- | --- | --- |
| 一次开始 + 三次空查询的下游状态 GET | 0 | 2；第一次实读 queued，第二次实读 found，第三次读取已确认的 found |
| 查询 RPC 内层正文 | 三次均 0 字节 | 三次均 0 字节，不增加任何字段 |
| 同账号重新登录另一条连接，空查询 | 无 GET | 无 GET，不接管旧票据 |
| 空停止请求 | 不取消下游 | 不取消下游，并禁止后续隐式轮询 |
| 下游 HTTP 503，正文却写 found | 不作为成功对照 | 标记 unavailable，不认领票据，不自动轮询 |

这是服务端兼容策略，不是声称零字节就是零售客户端的查询格式。只在回环匹配器、正常绑定的同一 RPC 连接、请求者等于登录玩家、模式表精确识别 PvE 且下游确认票据时，对唯一开始后的空查询执行只读轮询。重复开始、未解码或含未知内容的查询、重新绑定、普通 PvP 均不会触发隐式 GET；显式票据继续走原接口。当前只记录选择、下游状态、查询来源与回包长度，不输出账号、票据、地址或正文。既有服务日志仍应保留在本机。[会话实现](session-progress.cjs) · [发布包 A/B](release-menu.test.cjs)

**接入**：在已规范为回环的托管应用暂存副本中，除原 `native-menu.cjs` 外，同级部署 `session-progress.cjs` 并应用 `session-progress.patch`。主线需将两件资源一起嵌入、注册；本次未改组合器或正式安装。补丁已在原发布包隔离副本完成正向应用及反向校验，也已在本机组合器产物的入口逐块核验唯一上下文。不能直接对仍指向外部匹配器的原包启用该服务。[接入补丁](session-progress.patch)

在本目录执行：

```powershell
node --test verify.test.cjs release-menu.test.cjs session-progress.test.cjs
node menu-trace.cjs --sessions 'C:/BrtWork/SDK/installer_tool/login-server.log'
```

系统 Node 24.14.1 和随附 Node 24.14.0 各通过全部 **15/15** 项，失败 0。主线暂存应用 `099d3f68…` 的完整入口另通过逐块唯一上下文检查 1/1。临时档案服务与 HTTP 匹配夹具均已关闭。

普通历史档案服务日志 2 份（含备份，未按独立会话去重）共记录 4 条列表查询、0 条开始、0 条找到。安装旁 24 份启动日志和 1 份客户端日志也没有可关联的开始或载图记录。上述旧日志运行 `--sessions` 返回 `attempts=[]`、`unassociated=0`。主线可在接入后对一次真实选择的**单进程**日志运行同一命令，按尝试序号核对模式来自表行还是匹配字符串、下游状态、查询途径及回包字节数。仍缺真实点击后客户端接收分配、地图载入与可操控的连续证据。本次未连接公网、启动游戏、使用生产端口或读写正式玩家档案。[脱敏日志读取](menu-trace.cjs)

## V0.8.4 发布包复核

V0.8.4 的普通会话已验证原生 PvE 菜单的列表和区域查询。客户端接收分配并进入关卡的消息仍待确认。发布方公布的 `Release.zip` SHA-256 为 `fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094`，本地缓存的摘要相同。压缩包内 `project_rebound_version.txt` 写作 `v0.8.2`。发行说明中的默认 PvE 设置属于 `ServerLauncher`。[发布资产及摘要](https://github.com/STanJK/ProjectRebound/releases/expanded_assets/V0.8.4) · [发行说明](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4)

| 核对项 | 结果 |
| --- | --- |
| 发布包普通会话实测 | 原包 `QueryPlayList` 返回 `PVP/Playtest/Purge`，区域 ID 为空。在临时副本上应用现有 `playlist.patch` 并随附 `native-menu.cjs` 后，同一路由返回 `Rush_PVE_Easy`、`Rush_PVE_Normal`、`Rush_PVE_Hard` 和原 PvP 条目。`QueryUnityMatchmakingRegion` 与该服务 `GET /` 发布的区域 ID 一致。 |
| 匹配接口静态核对 | 发布包 `index.js` 第 759、812、855 行分别处理开始、查询、停止。服务向本机匹配器入队，取到 `found` 的 `serverIp/serverPort`。开始响应只定义 `StatusCode`，查询处理器第 851 行构造 `{}`。UDP 处理 QoS，第二个 TCP 监听的 `data` 回调在第 1102 行只记录来包。 |
| 分配消息候选 | 发布包共含 41 个 proto。`gameplay.proto` 的 `PlayerJoinMatchReq` 只有 `userId`，响应为空。聊天和队伍的 `JoinReq` 分别传 `roomId`、`partyId`。普通会话调用 `QueryNotification` 实收两条补丁说明及一条社区公告。SDK 中 `OnPlayerJoinGameDelegate` 为无参事件，`EPBMatchmakingStatus::Complete` 是客户端状态。把这些事件关联到具体入局消息，还缺同一次成功匹配的客户端接收记录。 |

UnityMatchmaker 的公开 SDK 声明还区分了 `FUMAllocationPayload` 的对局数据和 `FUMConfig` 的 `ConnectionIP/ConnectionPort`。社区旧版普通 HTTP 票据接口 `GET /v1/matchmaking/tickets/{ticketId}` 可返回 `connect` 和 `joinTicket`。发布包服务端实际轮询 `/matchmaking/status/{ticketId}`。这些地址字段到原生客户端入局消息的映射仍待确认。SDK 数据结构 · HTTP 票据接口

这份发布包 `index.js` 的 SHA-256 为 `14ae33e2731895279f39fdfaf6295cd59bc2e2a8deea780e166dd5665d01ad89`。上一轮的组合器只识别 `3961c933…` 本机基线。后续 profile-runtime 已分别核验发布包和独立 MetaServer 来源；这与本轮会话补丁的嵌入是两个独立步骤。[组合器支持范围](../profile-runtime/README.md) · [安装来源](../../src/core/install_ops.rs)

### 教程与靶场

三个原版军械面板的 `TestWeaponInputAction` 均指向 `DT_InputActions.GoRange`。军械界面管理器 `UPBCustomizeUIManager` 公开 `GoToRange(float)` 和 `ExitRange(float)`，本地玩家以 `RangeLevel` 引用靶场 World。教程页有独立的 `GotoTutorial(FName)` 和 `TutorialLevels` 接口。现有普通导出仍缺本地玩家默认对象的 World 引用值，以及各教程键与包名的对应表。武器编辑面板 · 军械界面管理器 · 本地玩家声明

历史客户端会话的服务端日志记录了大厅进入靶场 1 次、靶场返回大厅 1 次，展厅到靶场及反向各 8 次。主日志第 46 行有播放列表查询。该日志和备份中的匹配开始及进入 `InTutorial` 记录均为 0。发布包初始化状态为 `InMatching`，首次转为 `InHall` 也可能来自初次进入大厅。大厅进入靶场 · 展厅进入靶场 · 备份会话

2026-11-13 的客户端 stdout 指出了 `clientlogs/clientlog-20261113_141338.txt`，该文件为 0 字节。同期 headless 记录标明客户端停在登录之前。另查安装旁 `Win64/logs` 的 24 份历史启动文本，对 `StartUnityMatchmaking`、`QueryUnityMatchmaking`、`MatchFound`、`ClientTravel`、`Browse`、`LoadMap` 和游戏地图名称的检索均为 0 条。当次 stdout · 客户端日志文件 · 当次运行记录

隔离实测通过 `POST /connectServer` 取得合成账号会话，随后在同一 RPC 连接用 `SetPresence` 写入并用 `QueryPresence` 逐一读回 `InHall → InShootingRange → InShowRoom → InShootingRange → InHall → InTutorial → InHall`。服务使用进程级 `PartyPresence` 保存这些状态。PvE、教程和靶场的游戏内实际点击、地图载入及可操作状态仍待验收。PvE 协议缺口是同一次正常匹配中客户端收到的消息路径和地址字段，以及该消息后的地图载入记录。

### 本轮复验

在本目录执行下列命令。`BOUNDARY_RELEASE_084` 和 `BOUNDARY_UI_PROPERTIES` 可指定同版本的本地发布包和 UI 属性导出。Node 24.14.1 与 24.14.0 各通过 4 项窄测试。本次使用本目录下的临时应用副本、回环地址和操作系统分配的端口。日志工具输出事件计数与首末行号。[发布包窄回归](release-menu.test.cjs) · [日志摘要工具](menu-trace.cjs)

```powershell
node --test release-menu.test.cjs
node --test --test-name-pattern='original cards|all seven allowed|tutorial keys' verify.test.cjs
node menu-trace.cjs 'C:/BrtWork/SDK/installer_tool/login-server.log'
```

本轮另跑的 3 项内容测试通过，覆盖原菜单控件、7 种 PvE 模式与地图组合、教程键与具体 World 的区分。本地包清单可解析 `/Game/Maps/EA/Tutorial/Tutorial_Radar`。以 `Weapons` 菜单键查询时，工具返回 `level-mapping-required`、空参数及退出码 2。

## 前轮快照记录

2026-11-20。补丁使本机 MetaServer 的原有 RPC 返回三个 PvE 条目，以及与本机发现数据相同的区域 ID。隔离测试让本机匹配夹具返回测试地址，经原服务编码后，发给客户端的查询正文为 0 字节。原生 PvE 按钮进入对局仍缺少客户端接收分配的接口依据。教程有原作菜单控制器和本地 World。两个按钮的真实游戏内点击、地图载入与可操作状态均未验证。

## 人机入口

主菜单卡片把 PvE 的 `PageClass` 设为 `UMG_Assignment`，`PlayListType` 设为 `PVE`。通用卡片将 `btn.OnClicked` 绑定到 `…OnButtonClickedEvent__DelegateSignature`。主菜单绑定 PvE 的 `StartMatch`，任务页实现 `K2_OnPlaylistUpdated`。SDK 提供 `QueryPlaylist(InPlayListType)` 和 `BP_FindGatheringMatch(InSearchingGameModes)`。这些是静态绑定和接口。历史档案服务日志另有 `Query Playlists!`，该行未记录触发卡片及选择项。卡片点击 · PvE 绑定 · PvE 页面 · 匹配 SDK · 历史播放列表请求

| 模式表行 / 补丁的 GameModeList | 原表 MatchmakingString | MapsForMatch 原始行名 |
| --- | --- | --- |
| `Rush_PVE_Easy` | `EASY BOT` | `warehouse`、`DataCenter` |
| `Rush_PVE_Normal` | `NORMAL BOT` | `MiniFarm`、`CircularX`、`OSS` |
| `Rush_PVE_Hard` | `HARD BOT` | `OSS`、`CircularX` |

七种组合逐一核对了地图包、难度子关卡和 `BP_PBGameMode_Rush_PVE_*` 类。表里的 `warehouse` 指向包名 `Warehouse`。原有服务唯一示例 `Purge` 的行名与匹配字符串相同，无法据此区分客户端需要哪一种。补丁选用行名作为列表值，适配器识别两种名称。真实点击后发出的字符串仍待记录。中英文列表中的中文标题为集成提供的显示文本。模式表 · 原版包清单 · 社区工程使用的模式类

### 本机消息

下表按原服务的 `/matchmaking.Matchmaking/` 路径列出。隔离回归由测试端构造输入，在同一条 TCP 连接上使用原处理函数、protobuf 和消息封包。下游是自建的回环 HTTP 匹配夹具。各次请求按相同的消息 ID 与路径收到响应，最后一次查询后的 120 毫秒观察期内额外收到 0 字节。

| 原生 RPC | 服务端行为与实际回包 |
| --- | --- |
| `QueryPlayList` | 基线只有 `PVP / Playtest / Purge`。补丁保留原有条目，在 `PlaylistsJSON.PVE` 增加三个难度。模式表缺失时回传原列表。 |
| `QueryUnityMatchmakingRegion` | 基线用数组下标读取 `regionid`，回包缺少 ID。补丁从同一发现记录取 `region_id`，回包 ID 与发现 ID 相同。 |
| `StartUnityMatchmaking` | 请求的 `Payload` 与 `GameMode` 产生 `POST /matchmaking/enqueue`，带用户、区域、模式和新票据。省略 `GameMode` 时服务实际排入 `Purge`。等待和立即找到服务器均返回内层十六进制 `08 00`，其中只有 `StatusCode=0`。 |
| `QueryUnityMatchmaking` | 测试端从自己的 HTTP 夹具取得票据轮询。下游 `GET /matchmaking/status/{ticketId}` 返回 `found` 和本机地址后，原服务仍编码 `{}`。等待、找到、立即找到后的查询以及无票据查询，客户端收到的正文均为 0 字节。 |
| `StopUnityMatchmaking` | 带票据的请求向下游发送 `POST /matchmaking/cancel/{ticketId}`，客户端收到空正文。 |

本机服务的状态查询丢弃了已计算的地址。其扩展 proto 自身注明字段暂定，`QueryUnityMatchmakingRes` 为空。社区历史工程另有带用户身份的 `/v1/matchmaking/tickets`，返回的 JSON 可以包含 `connect` 与 `joinTicket`。当前原生路由调用的是上表中的管理接口。把历史 HTTP 字段直接塞进 protobuf 缺少客户端依据。先前的专服记录只验证到监听，文件中 `gameClientJoinVerified` 和 `uiVerified` 均为 `false`。本机同版服务 · 查询回包 · 匹配 proto · 历史票据接口 · 专服历史验证

## 教程入口

原卡片的父节点名保留原拼写 `TUTORAIL`，PvE 是 `ASSIGNMENT`。通用卡片有 `GotoPage` 调用结果。该拼写在这里作为原数据标识，客户端按下后的导航仍待实机观察。导航调用 · 教程父节点

主菜单教程卡片指向 `UMG_Tutorial`。子控件的 `Btn_Clicked` 有绑定，导出图中有获取 PlayerController 和转换到 `PBPlayerController_Menu_BP` 的节点。其原生父类提供 `GotoTutorial(FName)`，本地玩家类提供 `TutorialLevels: FName -> World`。现有属性导出能证实这些组件和接口，缺少这条蓝图的完整调用体及本地玩家类默认映射值。教程页面 · 按钮与控制器 · 控制器接口 · 本地映射声明

教程页依次配置 `Movement`、`Weapons`、`Combat`、`Scout`、`Simulation`。前四项没有显式折叠，第五项默认 `Collapsed`。原版包清单有 `/Game/Maps/EA/Tutorial/` 下的 `Tutorial_Movement`、`Tutorial_Shoot`、`Tutorial_Combat`、`Tutorial_Radar`。工具支持按确切包名生成标准地图启动参数。按菜单键查询时，工具只接受来自普通本地 `PBLocalPlayer_BP` 默认对象的映射，再核对包清单。缺少映射时输出 `level-mapping-required` 和空参数。教程页无需在 PvE 播放列表中添加任何条目。五项配置及可见性 · 教程世界清单 · 地图接口

## 打包与复验

主线程打包在用的 MetaServer 版本时，将 `native-menu.cjs` 与包内 `index.js` 同级放置，在该暂存副本上应用 `playlist.patch`。补丁仅改区域数据读取和播放列表响应。与其他更改合并时可先用 `git apply --check` 核对上下文，回退用 `git apply -R`。本次已在新建副本应用补丁并检查反向补丁。验证快照的 `index.js` SHA-256 为 `3961c933f8a3396fb01d9bb7e190db2312ce5a4c2320ea78348aed84df5ce10d`，既有安装记录已核对该快照。安装版本记录 · [服务补丁](playlist.patch) · [打包模块](native-menu.cjs)

在本目录执行以下 PowerShell 命令。后两条只生成单个 `argv` 元素，供标准游戏启动入口使用。地图能否在当前 Shipping 客户端直接载入、初始化 AI 或触发教程目标，仍待客户端验证。

```powershell
node --test verify.test.cjs
$defs = 'C:/BrtWork/SDK/sdk-fixture/validation-server/game/definitions/DT_GameModeDefinition.json'
$worlds = 'C:/BrtWork/Assets/reports/all-source-paths.txt'
node native-menu.cjs pve $defs 'NORMAL BOT' CircularX
node native-menu.cjs tutorial-world Tutorial_Radar $worlds
```

`node native-menu.cjs tutorial Weapons [LOCAL_PLAYER_EXPORT PACKAGE_LIST]` 使用菜单键。提供默认对象导出和包清单时输出核实的 World。退出码 `0` 表示成功生成结果，`2` 表示需要键映射，`1` 表示输入错误。复验可通过 `BOUNDARY_META_SNAPSHOT`、`BOUNDARY_MENU_EXPORTS`、`BOUNDARY_PACKAGE_LIST` 指定同版本的普通本地输入，依赖使用快照自带的 `protobufjs`。

系统 Node 24.14.1 与启动包自带 Node 24.14.0 均通过 5 项测试。测试从普通本地副本按白名单复制服务代码、模式表和协议文件，在子目录临时副本内仅启用原 TCP 路由，RPC 与管理接口分别绑定 `127.0.0.1:0`。覆盖基线、补丁、缺表回退、PvP 对照、七种 PvE 组合、四个教程 World 及隐藏项。运行结束已关闭测试进程并回收临时目录，全部写入限于本子目录。[隔离回归](verify.test.cjs)

## 客户端验收

真正游戏内点击的状态为**未验证**。本次没有启动图形客户端或游戏专服。PvE 验收需要在同一次正常会话关联按钮选项、实际 `StartUnityMatchmaking` 模式、合法分配的交付消息、载入地图和可操作角色。教程验收需要记录各可见选项进入的地图、首个教程目标及返回菜单结果。原始录屏或正常客户端日志可用于补齐，按地图包名直接启动的成绩另记。

## 2026-11-21 公开入房方式核对（独立于补丁回归）

**结论：没有查到 V0.8.4 或零售游戏公开支持的普通接口，可在玩家已于原版大厅点击人机后，把同一运行中的客户端送入指定本机房间。** 有文档的三个动作，调用方与效果不同：

| 来源 | 文档确认的动作 | 不能据此认定 |
| --- | --- | --- |
| 原作 Steam 公告 | 2023-12-27 《Year End Major Update!》：主菜单选 **PvE → Easy / Normal / Hard**，当时只有 Rush，1 人即可开始，最多 5 人。首页 Quick Match 则选首个可用对局。2024-06-19 发行方另有停服公告。 | 前者是原作历史 UI 流程，后者不是“选本机服”按钮；公告没有给出从当前大厅输入地址、选择本机房间或接受外部委托的方法。[原作 PvE 公告](https://steamcommunity.com/app/1364020/allnews/#:~:text=Main%20Menu%20Adjustments) |
| V0.8.4 标签中的社区房间浏览器 | Backend API **Spec v2** 使用 **HTTP/1.1、URL 前缀 `/v1`**。玩家在**外部桌面浏览器**刷新房间列表、选择房间、点 **Join**；`POST /v1/rooms/{roomId}/join` 给调用它的浏览器预留位置并返回 `connect`、`joinTicket`、`expiresAt`。随后浏览器**启动游戏客户端**。上游报告的双机成功也是这条启动流程。更早的 Spec v1 将 `/v1/match/resolve` 列为用于生成启动地址的推荐项。 | 不是原生菜单 RPC，也不是给已开的大厅发跳转。源码标签有原型使用文档，不等于 Release.zip 附带可直接点的浏览器：本地 `tar -tf` 只列包内 1143 项，未见原型 README 指定的浏览器 EXE、脚本或启动批处理。[标签版 API](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/docs/backend-api-spec-v2.md) · [同版加入流程第 7 节](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/docs/game-and-server-launch-flow.md) · [原型 README](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Desktop/ProjectRebound.Browser.Python/README.md) · [Spec v1](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/docs/backend-api-spec-v1.md) |
| 后续开发主线 README 的 `ping/pong/join` | 启动器向**社区进程内 Payload** 发 `ping` 或 `join`，由 Payload 返回 `pong` 或 `join_ack`；前者是存活回应，后者仅表示目标已校验并排队。协议手册没有声明可据以兼容零售版的数字版本。这也不是标签文档中用于验证 UDP 连通性的 `pong`。 | 不是档案服务、原版大厅对外的 HTTP API 或载图回执，且依赖本任务排除的客户端组件。V0.8.4（`b3d7f83`）发布于 **2026-05-16 03:03:59 UTC**，说明只提默认开 PvE。协议移植及手册提交 `5999cdfd` 在 **2026-08-04 02:26:18 UTC**，是该标签之后第 6 个提交；核查时该仓库最新正式版仍为 V0.8.4。不能把开发分支的说明当成该发布包承诺，也没有检查旧二进制来推断未文档化的行为。[发布说明](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4) · [固定提交的协议手册](https://github.com/STanJK/ProjectRebound/blob/5999cdfdae938bbd7e48a1cb40a51477f823d8fd/Docs/ForAI/08-Named-Pipe-Command-Protocol.md) · [版本关系](https://api.github.com/repos/STanJK/ProjectRebound/compare/b3d7f83f3789111d4a460b9662a8dc4e06e6a934...5999cdfdae938bbd7e48a1cb40a51477f823d8fd?per_page=1&page=2) |

**正常日志复核**：本机 档案服务日志及其备份共 2 份（不作两个独立会话统计），有 4 行列表查询、0 行匹配开始；安装旁 `Win64/logs` 的 24 份启动日志和 `Win64/clientlogs` 的 1 份空日志也未找到加入回执、客户端跳转或载图标记。没有同一次点击后由原大厅接收目标、再由客户端载图或游戏服接纳该会话的连续证据。除非取得**明确面向当前已登录客户端**的发布版使用文档，或同会话正常日志补足上述环节，否则维持未证实，不把房间预留、命令排队、端口监听算作入房。本次未启动游戏、未调用游戏/调试接口、未修改生产源码。

## 2026-11-21 原版人机地区选择

原版人机页 `UMG_Assignment` 嵌入了 `UMG_MatchmakingRegionList`。这个摘要控件处理已选地区字符串。地区弹窗的原生更新回调接收可用的 `PBMatchmakingRegionInfo[]`，元素有 `RegionID` 和 `RegionName`。弹窗属性导出同时列出按行名查找 `PBMatchmakingRegionAddressRow`、创建地区行和加入网格的节点。地址行定义含 `RegionDisplayName` 和 `RegionAddressName`。单行控件另有 `K2_RefreshQosData` 和 `QueryPingState`。当前零售包清单收录了 `DT_MatchmakingRegionAddress`，已有普通导出尚缺该表的行内容和查表输入连线。哪个字段能命中零售行名、QoS 是否影响行可见性均待证。人机页组件 · 可用地区回调 · 弹窗查表 · 单行 QoS · 零售包清单

社区 MetaServer 的 `GET /` 发布 `location_id/region_id/ipv4/ipv6/port`。已登录会话的 `/matchmaking.Matchmaking/QueryUnityMatchmakingRegion` 返回 `StatusCode` 和重复的 `RegionId/RegionName`。BRT 原先把独立 main 的地区目录替换成 V0.8.4 的一行。新增 `region-catalog.patch` 复用 `native-menu.cjs`，只对已验证来源及对应的本机发现条目恢复上游发布的配对。[固定 main 的发现条目](https://github.com/STanJK/BoundaryMetaServer/blob/007850d0fd07c688d0a1ec1ff2a8771726c3463c/index.js#L19-L31) · [固定 main 的名称配对](https://github.com/STanJK/BoundaryMetaServer/blob/007850d0fd07c688d0a1ec1ff2a8771726c3463c/index.js#L146-L151) · [地区协议](https://github.com/STanJK/BoundaryMetaServer/blob/007850d0fd07c688d0a1ec1ff2a8771726c3463c/game/proto/Response/QueryMatchmakingRegionResponse.proto) · [V0.8.4 发布包](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4)

| 托管副本的原始来源 | 修订前地区数 | 修订后 HTTP 和 RPC 逐项相符 |
| --- | ---: | --- |
| ProjectRebound V0.8.4 发布包，入口 `14ae33e2` | 1 | 1，`6 / us-east1` |
| 本机同版档案服务，入口 `3961c933` | 1 | 1，`6 / us-east1` |
| 独立 BoundaryMetaServer main，提交 `007850d0` | 1 | 2，`6 / us-east1` 和 `10 / asia-east2` |

第一项 ID 是 `336d1f3e-3ecb-11eb-a7dc-3b7705f20f56`。第二项 ID `fadebabe-0000-4000-8000-3b65e9898dbb` 是社区 main 源码的字面值。两行指向同一台本机 QoS 服务，实际机房、零售行名和中文地名均待独立证据。表中基线已含先前修正空 ID 的 `playlist.patch`。

**已有实际请求。** 预览目录的 `orbital_profile/login-server.log` 最后写入时间为 2026-11-21 04:06:29（北京时间）。第 62、175 行收到地区 RPC，这份日志中 `GET /`、UDP QoS 请求和开始匹配各 0 次。旧版仅记录 RPC 到达，回包行数及当时界面行数均未知。该目录检查时仅有一份托管应用清单，来源为 `local-archive-3961c933`。单凭目录和行号无法将两次请求分别归到某次人机点击。[现有服务日志](../../target/player-preview/orbital_profile/login-server.log#L62)

**隔离回归。** 从三个来源的代码及协议分别生成生产 `prepare` 托管副本，只使用临时档案路径、合成账号和随机回环端口。对每个原样生成的副本检查 HTTP 发现、正常登录后的地区 RPC 和实际收到的 UDP 回声，再撤下本补丁对照，重新应用后逐字节还原入口。系统 Node 24.14.1 和随附 Node 24.14.0 分别执行 `node --test integrations/native-menu/*.test.cjs`，均为 **28 通过，0 失败**。界面显示仍待真人确认。[三来源回归](release-menu.test.cjs) · [原版控件及降级回归](verify.test.cjs)

**同次点击的采集方法。** 进入人机前先运行一次下列命令，打开地区选择后再运行一次。对照新增行号及次数。`regionRequest` 是收到查询，`regionReply` 是服务端完成回包写入，`replies` 列出行数、非空 ID 数和白名单名称。`discoveryRequest` 与 `qosRequest` 分别计数。输出只含事件、行号和已允许的地区名称。用户可同时记录弹窗可见行数。若 RPC 写出非空行而弹窗仍空，应对照同次界面与零售地址表行键。[只读诊断工具](menu-trace.cjs)

```powershell
node integrations/native-menu/menu-trace.cjs --regions target/player-preview/orbital_profile/login-server.log
```

## 2026-11-21 地区弹窗独立验收

### 已观察

| 原作入口 | 输入或数据 |
| --- | --- |
| 人机难度卡片 | `UMG_PlaylistItem` 绑定 `Btn_Clicked`。其图中有匹配管理器的 `IsMatchmakingNeedsRegionInfo` 布尔结果。 |
| 人机页地区摘要 | `ButtonMain.OnReleased`。摘要激活时取得 `IsInParty` 和 `IsPartyLeader`，带队长、成员两套显示回调。成员提示原文是 `Only the captain can choose the region`，摘要设计默认文字是 `Default Area`。 |
| 地区弹窗 `TERRITORY` | `REFRESH` 对应 `Button_371.OnReleased`，`CONFIRM` 对应 `Button_661.OnReleased`，`CANCEL` 对应 `Button_836.OnClicked`。单行用 `ButtonMain.OnClicked`，确认及取消选择的委托参数是 `FName MatchmakingRegionName`。 |
| 可选地区与已选名称 | 匹配管理器分别广播 `PBMatchmakingRegionInfo[]` 和 `FString[]`。弹窗持有这两个数组，摘要订阅已选名称。原作网格预置三个只有布局属性的地区控件。 |
| 开始匹配 | 已发布的 Start 请求在 `Payload.UnknownMessage[].RegionId` 携带选择，档案服务读取该字段。诊断前日志中 Start 计数为 0。 |

难度卡片 · 队长判断 · 两种地区更新 · [原作按钮与布局的离线断言](region-acceptance.test.cjs) · Start 协议

主动检查前，预览服务日志共 289 行，最后写入于北京时间 **2026-11-21 04:06:29**。地区请求行自身缺少时间戳。对该文件运行 `menu-trace.cjs --region-window` 得到以下两个区间。两段的地区回包写入记录、QoS、HTTP 发现、Start 计数均为 0。该版本的地区处理器只记录请求到达。

| 实际日志区间（左开右闭） | 依行号排序的观察 |
| --- | --- |
| `(45, 70]` | 第 46 行查询列表，第 47 行 `InMatching -> InHall`，第 62 行查询地区，第 70 行 `InHall -> InShowRoom`。 |
| `(172, 198]` | 第 173 行 `InShowRoom -> InHall`，第 174 行查询列表，第 175 行查询地区，第 198 行 `InHall -> InShowRoom`。 |

**现有回环服务。** 2026-11-21 06:30:54 核对 Windows 监听归属。PID `44224` 自 04:01:23 运行预览副本 `5769f191…`，来源为 `local-archive-3961c933`，入口 SHA-256 为 `0e1728f704294b6bbff7d9bd8ea124a442e46a28a58b8966c110fb79a488b1c0`。同一进程在回环地址监听 HTTP、原生 RPC 与 QoS。当时零售客户端进程数为 0。下面两次诊断只连接该进程，北京时间如下。

| 时间及日志区间 | 实际收到的内容 |
| --- | --- |
| 06:32:09.743 至 06:32:09.976，`(289, 312]` | 匿名 `GET /` 返回 HTTP 200 和 1 个发现条目，`location_id=6`，地址为 `127.0.0.1:9000`。向该端口发送一次 QoS 探测，从同一端口收到带原探测尾部的 24 字节应答。第 294 行为 GET，第 296 行为诊断标识，第 303 行为收到 UDP。 |
| 06:34:53.693 至 06:34:53.748，`(312, 347]` | 用新生成的诊断标识经 `/connectServer` 建立一个本机临时会话。第 321 行有诊断标识，第 342 行为会话绑定，第 343 行查询播放列表，回包内有 3 个 PvE 项。第 344 行查询地区，诊断端实际解码 `StatusCode=0`、正文 52 字节、1 行，`RegionName=us-east1`，`RegionId=336d1f3e-3ecb-11eb-a7dc-3b7705f20f56`，与 HTTP 发现相符。 |

已断开诊断连接。按该版本的服务实现，签发的诊断令牌条目保留在该进程的内存会话表，直至进程退出。诊断只访问发现、QoS、会话、播放列表和地区路由。证据文件只含已解码的地区消息。[实际回包](evidence/20261121-preview-region-reply.json) · [服务收到地区查询的记录](../../target/player-preview/orbital_profile/login-server.log#L344) · [地区消息处理器](../../target/player-preview/orbital_profile/profile-runtime/apps/5769f191e00b20c91d0015617d3fccf4f3bec8dfa311bebbcd24d32e7280ad63/index.js#L750)

零售包普通目录清单收录了 `GameData/CSVConfig/Matchmaking/DT_MatchmakingRegionAddress` 和同目录的 `DT_CountryRegion`。核对了 Steam 安装、启动工具目录、两份 UI 属性导出、已有资源索引及 UI/旧版 ZIP 目录。V0.8.4 发布包中与地区同名的项目只有响应协议。上述资料能定位资产，地址表行键仍缺。两份 UI 属性导出的弹窗均包含 52 个相同名称的图局部变量，均有按名查 `PBMatchmakingRegionAddressRow` 的结果。具体输入连线仍缺。资产清单 · 查表结果

### 由代码推断

队长状态会影响地区选择入口。Steam 安装中 Win64 下的档案服务源码和验证快照的 SHA-256 均为 `3961c933…`。旧预览托管副本有相同的成员处理逻辑。会话绑定 `socket.playerId` 后，`Create.PartyMembers` 和 `QueryPresence.PartyMembers[].UserId` 仍取固定示例值。对不等于该值的会话，服务端公布的成员与登录会话不同。现有证据不能确定该差异决定了玩家的队长身份，或导致已经打开的弹窗为空。建队响应只有状态、队伍 ID、成员三项。如实机出现队长提示，可将此发现交档案服务负责人核对。会话绑定 · 建队成员 · 成员状态

离线运行该历史源码自身的 `BuildRegionList()` 和发布版 protobuf。历史源码生成的正文为 **14 字节、1 行**，名称 `us-east1`、ID 为空。人工构造的空数组对照正文为 **2 字节、0 行**（`08 00`）。运行中的托管副本已修正这个 ID。其磁盘处理器用同一发现条目生成的 52 字节回包，与本次实际 RPC 的字段及长度相符。零售端如何处理缺失 ID、地址表查找失败或 QoS 未完成仍待实测。[离线协议回归](region-acceptance.test.cjs:87)

### 仍需玩家

1. 在同一份、同一次服务启动的日志中记录进入前的 `throughLine`。单人或已知队长进入人机，记下看到的是摘要、成员提示，还是 `TERRITORY` 弹窗，以及实际行数。
2. 弹窗打开后记录一次区间，只点一次 `REFRESH`，待刷新结束再记录下一段。若有可选行，则点击一行、点 `CONFIRM`，记录摘要和之后的 Start。若始终零行，则记录零行和退出弹窗。每步使用上一段的 `throughLine` 作为新的 `AFTER`，日志轮转或服务重启后重新取起点。单独一次刷新可以复用已缓存的地区数据，服务查询与客户端显示须分别记录。
3. 取得同版的普通地址表导出后，可用 `region-keys.cjs` 与本次实际 RPC 对照。工具记录表文件 SHA-256、包来源和实际行键，分别检查 `RegionName` 和 `RegionId` 并保留原键拼写，只对 ASCII 忽略大小写。图的查表输入连线仍须从同版原作资料确认。[UE 4.27 的 FName 比较规则](https://dev.epicgames.com/documentation/en-us/unreal-engine/fname?application_version=4.27)

```powershell
$log = 'target/player-preview/orbital_profile/login-server.log'
node integrations/native-menu/menu-trace.cjs --region-window $log
node integrations/native-menu/menu-trace.cjs --region-window $log 172 198
node integrations/native-menu/region-keys.cjs $tableFile integrations/native-menu/evidence/20261121-preview-region-reply.json
node --test integrations/native-menu/region-acceptance.test.cjs
```

系统 Node 24.14.1 和随附 Node 24.14.0 的新增离线验收各 **7/7**，另选取既有地区日志、原作控件和自定义目录降级回归 **3/3**。本次改动只涉及离线验收、只读日志和本实录。

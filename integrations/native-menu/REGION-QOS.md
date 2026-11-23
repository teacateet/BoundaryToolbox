# 19.20.21 地区工作日志

## 玩家会话

2026-11-21，北京时间。玩家解包目录 `target/player-preview/19.20.21/player/orbital_profile/` 的 `login-server.log` 最后写入于 22:42:03。下述行号属于该次 117 行快照，单行没有时间戳。

| 行号 | 服务端观察 |
| --- | --- |
| 46、47 | 查询列表，状态从匹配进入大厅 |
| 75、103 | 从大厅进入展示间，再回到大厅 |
| 104、105 | 收到地区 RPC，完成响应写入。共 1 行，1 个非空 ID，名称 `us-east1` |

窗口内 HTTP 地区发现、可识别的 QoS 请求、开始匹配的记录均为 0 次。旧处理器对所有首字节为 `59` 的消息都会计数。零售地区地址表确有 `us-east1` 行。可用地区列表与客户端确认的地区名是两个状态。玩家反馈仍未选区。现有记录无法定位客户端在哪一步停止，也无法确认客户端实际请求的 QoS 地址或是否保存选择。

只读复核，从仓库根目录运行。输出只含允许的事件和地区摘要。

```powershell
node integrations/native-menu/menu-trace.cjs --region-window target/player-preview/19.20.21/player/orbital_profile/login-server.log 0 117
```

## 服务兼容修复

现有 ZIP 的 SHA-256 为 `434d5b15fbbac163ec2f265bb740b8dccf73b0314aa6f15c607bd8ac71d11e19`。包中有 `region-catalog.patch`，解包后的托管入口仍从固定偏移 11 取探测数据，并把序号写成 0。回环 HTTP 发现提供 QoS 地址，地区 RPC 提供 ID 和名称。两者的可达性可分别验证。

Unity QoS v0 在标题长度字节和 UTF-8 标题之后写入序号、标识符和时间戳。响应头为 `95 00`，其后需保留这 11 字节。对一个单字节标题、序号 1 的有效请求，三个来源在改动前都只回 `95000000000000`。修订后均回 `9500013412e803000000000000`。[Unity QoS 1.0.0 官方源码包，见 QosRequest.cs 与 QosResponse.cs](https://download.packages.unity.com/com.unity.services.qos/-/com.unity.services.qos-1.0.0.tgz)

新增独立的 `region-qos.patch`，保留原地区目录补丁的独立识别。测试使用临时服务副本、合成会话和随机回环端口。每个来源验证三种标题长度、非零序号、ID 和时间戳，以及不完整请求和不支持的版本。单独撤下 QoS 修复后，相同地区列表仍可查询，响应再次与协议不符。重新应用补丁后，入口文件逐字节还原。

```powershell
node --test integrations/native-menu/region-acceptance.test.cjs integrations/native-menu/release-menu.test.cjs
```

本次 20 项通过。三个来源分别为 `projectrebound-release-v0.8.4`、`metaserver-main-007850d0`、`local-archive-3961c933`。

## 客户端能否配置 discovery 地址

- [工具箱人机入口](../../src/core/runtime_ops.rs#L522)启动大厅只传 `-LogicServerURL=http://127.0.0.1:8000`；`-match` 仅用于加入指定对局。本机社区 `startgame.bat` 也只列出逻辑服务参数。旧服务响应中的回环 RPC endpoint 和 `GET /` 中公布的 UDP 地址，都不能设置客户端首次发现用的 HTTP URL。
- 原作导出的 `ProjectBoundary/Config/DefaultEngine.ini` 第 107 至 113 行列出三项逻辑服务 URL、对局地址以及 `[SystemSettings] UnityMatchmaker.Region=1`，没有独立的 QoS 发现 URL。第 488 行另一项 `Region=1` 属于语音，不是匹配。没有证据说明修改数值地区项会切换 discovery 的地址。
- 本机普通设置目录为 `%LOCALAPPDATA%/ProjectBoundary/Saved/Config/WindowsClient`。`UnityMatchmaker.ini` 只有 CRLF（2 字节）；`Engine.ini`、`Game.ini`、`GameUserSettings.ini` 没有相应地址键。对当前终端、用户和系统环境只按 Boundary、Unity、Multiplay、Matchmaking、QoS 等相关前缀查变量名，均未找到候选。托管服务的 `MATCHMAKING_PORT` 不是游戏客户端的环境选项。
- 结论限于现有工具箱和可核实的公开设置：没有发现能合法指定客户端 QoS discovery URL 的选项。未读取玩家存档或凭据，未改设置、连接现场服务或观察外部流量。本地未收到请求，不等于请求去了外网，也可能尚未触发发现。Unity 将发现和探测定义为不同步骤；其 [QoS 流程文档](https://docs.unity.com/en-us/clanforge/legacy/quality-of-service-flow) 不是 Boundary 的启动参数说明。

## 玩家验收与发行

用户下次正常打开人机地区弹窗时，按同一时间窗口记录刷新后的可见行数、确认后的大厅摘要以及重开弹窗时的选择。同步对照上述命令的新增日志行。只有游戏内回读才能验证这次选择；若未收到 QoS 请求，需继续从客户端支持的发现地址和地区列表更新入手。

发行负责人须在 `tools/package-preview.ps1` 的随包组件清单中加入 `region-qos.patch`，以便随包的 `prepare.cjs` 可独立使用。托管资源登记和组件核对代码已包含新补丁。玩家现有 19.20.21 包仍使用旧处理器。

## 19.20.22 玩家会话

2026-11-21 北京时间。运行目录为 `target/player-preview/19.20.22/player/orbital_profile`。启动记录为 `logs/installer_20261121_233619.log`，服务记录为 `login-server.log` 第 1 至 197 行，最后写入时间为 23:38:08。

| 时间或服务日志行号 | 匿名观察 |
| --- | --- |
| 23:36:26 至 23:36:54 | 工具箱检测到一个已有登录服务，23:36:31 确认相关进程全部退出，23:36:54 启动新档案服务。TCP 6969、8000、9000 和 UDP 9000 的监听者均对应这次创建的 Node 进程，其入口位于 19.20.22 的托管目录。入口 SHA-256 为 `75929cb80588950b61f54807434b365649d565190a5f1b75a045f00f36e0db9f`，含 `region-qos.patch` 回包逻辑。 |
| 23:37:06 | 对局就绪并发布房间，工具箱启动原作大厅。 |
| 43 至 173 | 第 45、64 行的档案响应均含狙击手副武器 `SNIPER_RU-MOSIN`。第 78 至 172 行收到 28 次副武器更新，涉及 7 个已知武器 ID，每次后面都有保存记录。最后一次为 `SNIPER_RU-APS`，第 173 行记录保存。本次展示间操作后的档案回读和当前页显示待验。 |
| 183 至 185 | 展示间返回大厅。地区查询得到 1 行、1 个非空 ID，名称 `us-east1`，零售表对应南卡罗来纳州、北美。窗口内 HTTP discovery、已识别 UDP QoS、开始匹配各 0 次。客户端确认的地区和持久化选择待验。 |
| 23:38:32 | 租约停止续期。旧提示将三个进程的退出和 Windows 等待出错合并，无法追溯具体触发项。核对监听归属时同一新版档案服务仍在运行。 |
| 23:38:43 | 社区服务器列表刷新失败。该页面从社区列表接口读取服务器行，此次日志只记录顶层错误，底层原因未知。 |

本次会话中 `OpenProcess` 错误记录为 0 条。租约诊断修订使用原有同步句柄和 Windows 等待结果区分档案服务、对局启动器、对局。打开句柄失败时保留操作系统错误，等待失败时单独记录 Windows 错误。隔离回归使用本机测试事件、临时启动器及合成房间。

```powershell
node integrations/native-menu/menu-trace.cjs --region-window target/player-preview/19.20.22/player/orbital_profile/login-server.log 0 197
cargo test --release --locked --bin BRT local_pve_lease::tests -- --test-threads=1
```

### 原版业务回调交接

2026-11-22。静态样本是本机零售 `ProjectBoundarySteam-Win64-Shipping.exe`，SHA-256 为 `181c49ffb522b3eb01014c84fd9d3a2a5c0b66ae80a6a6addff4bdd6f8125843`。下列位置为该文件的 RVA，供 Halley 和地区维护者复核。

| 环节 | 原版业务语义 |
| --- | --- |
| `OnlineAsyncTaskQueryUnityMatchmakingRegion` | 任务名对应的响应处理入口为 `0x9ADC10`。外层错误码为零才解析正文。正文解析失败时任务记录 `-2`。解析成功即置成功标志，并单独复制正文状态和地区数组。 |
| 正常完成通知 | `0x9CC660` 将成功标志、状态码、地区数组作为三个参数发出。管理器 `0x1724AB0` 先检查成功标志，成功分支复制条目，在 `0x1724F31` 处向可用地区列表委托发出数组。内层状态没有与 `200` 比较。 |
| 消费的子项 | 原版 `matchmaking.UnityMatchmakingRegion` 的具名序列化方法 `0x9FCAE0` 分别以字段号 1 写 `regionId`、2 写 `regionName`。弹窗事件图由主线程核对，控件把业务结构的 `RegionName` 转为 FName，地址表查找成功后创建行，另设 `RegionID`。19.20.22 的 `us-east1` 有对应表行。 |
| 托管端对照 | 外层 `ResponseWrapper.ErrorCode=0`，正文 `StatusCode=0`、字段 2 是重复子消息。独立 Rust 服务将相同 RPC 的成功正文置 0，并从服务发现行生成 ID 和名称。[地区处理器](https://github.com/ProjectRebound/MetadataServerV2/blob/a239712be2a6df5966b8be1e20eef654bae8f2ee/src/packets/query_unity_matchmaking_region.rs) 和[地区消息定义](https://github.com/ProjectRebound/MetadataServerV2/blob/a239712be2a6df5966b8be1e20eef654bae8f2ee/proto/Response/QueryMatchmakingRegionResponse.proto)。 |

这条路径支持托管服务继续返回两个零。HTTP 发现接口成功时的 200 属于 HTTP 响应。19.20.22 用户会话中的可用列表广播、弹窗行数和本机确认地区仍待实机读回。房间退出提示修订的 7 项隔离回归全部通过。

### 东亚与南亚玩家的可选行

Halley 的 `.scratch/brt-region-contract-20261122/CONSUMPTION.md` 将本机设置 CN 对应到零售国家表的 Eastern Asia。原版在广播可选数组前，对 Eastern Asia 与 Southern Asia 用户只保留名称含 `asia` 的行。19.20.22 现场虽收到一行 `us-east1`，按此条件广播时为零行。

| 已验证服务来源 | 修订后的目录 | CN 过滤结果 |
| --- | --- | --- |
| local archive、V0.8.4 | 唯一既有 UUID `336d1f3e-3ecb-11eb-a7dc-3b7705f20f56` 对应 `asia-east2` | 1 行，香港 |
| 社区 main | 保留来源公布的 `us-east1` 和 `asia-east2` 两组 ID、名称 | 1 行，香港 |

单端点来源在其他国家也能选择同一香港行。main 的两个逻辑地区使用同一回环 QoS 端口和本机房间。主分支的亚洲 UUID 来自原服务目录。地区名称用于原作显示，均指向本机服务。

修订前用三份正常服务来源启动隔离托管副本，两种单行来源的 CN 可见行断言均为 `0 !== 1`，main 通过。修订后 `node --test integrations/native-menu/*.test.cjs` 共 37 项通过。各来源均核对 discovery、地区 RPC 和 UDP QoS，用实际查询得到的亚洲 ID 构造 Start 请求，收到成功回执并在服务端命中测试房间。Stop 后重新查询，行名和 UUID 一致。玩家界面确认、重开后的摘要以及实际进局仍待新版验收。

# 大厅下一局选图（本机人机）

进入 BRT 本机大厅，在原版「游玩 → 人机」列表点一张卡，就只对这次匹配生效。第一张「随机地图 · 随机人机难度」在以下 7 个组合中等概率选择；点原来的难度卡，在该难度的合法地图中随机。CircularX、OSS 还可以只指定地图，在这张图支持的难度中随机。取消后可重选，新一局不继承上一局的指定地图。

| 原版难度 | 可以选择的地图 |
| --- | --- |
| 简单人机 | Warehouse、DataCenter |
| 普通人机 | MiniFarm、CircularX、OSS |
| 困难人机 | OSS、CircularX |

房间所有者发布有效的单机 PvE 租约与本机控制连接时，档案才在原版 3 张难度卡之外发布 10 张卡。服务在点击时根据零售 `DT_GameModeDefinition.MapsForMatch` 确定组合并冻结地图；准备进程复查地图、难度、实际房间、拥有端口的游戏 PID 和发起选择的大厅 RPC。缓存已过期、当前房间不提供此功能或组合无效时，`StartMatchmakingResponse.StatusCode` 返回非零，不转交其他匹配器。普通 PvP 列表仍使用原匹配器。

**组队使用边界。** 当前托管档案的 [bindSoloParty](../profile-runtime/prepare.cjs) 给每个已登录的连接单独返回一个成员。已测试的单机房间认领一位大厅客户端的 RPC，再向它发送回环游戏地址；同一地址在其他电脑上指向各自电脑。发行版 PvP 的 `/matchmaking/enqueue` 向原匹配器提交玩家、地区、模式和票据，没有地图或整队成员参数。以上证据不能说明玩家历史上没有在 BRT 本机大厅组队；只能说明当前这条路径不能给三位玩家分配同一张指定地图。新增卡不用于组队 PvP。

要接这条多人路径，需要在一次实际组队匹配中对照：队长和队员是否共享队伍和匹配票据；哪个分房器收到地图及模式；游戏端是否把同一个可达的游戏地址发给全部成员；一人取消或掉线后票据怎样收回。诊断只需要脱敏的事件名、队伍成员数、各端点是否一致和地图/模式，不需要向队友开放本地档案、控制服务或内网。

可复用的两处实现是 [fixed_host](../../src/core/fixed_host.rs)（管理同一台专服）和 [friend_room](../../src/app/friend_room.rs)（邀请队友接入房主的游戏 UDP 端口）。二者都没有订阅原版大厅的整队匹配/取消事件。11 月 23、24、27、28 日四份交接档案日志里共 6 次开始匹配，结果只有 `queued`，未记录独立 MatchServer 的分配应答；不能把这四份日志认作过去的三人联机现场。

上游保留的 `Deprecated/TestProjects/Backend/ProjectRebound.MatchServer` 可参考其按地图、模式筛选房间的逻辑，但接口是 `POST /v1/matchmaking/tickets`，其 `MatchmakingService` 按 `ticket.PlayerId` 逐人占座，不会给整队保留一个房间。上游 `ServerListBackend` 的 `GET /servers` 是房间目录。两者都不能直接替代发行版大厅的 `/matchmaking/enqueue`，也没有证据表明任一服务参与了那次三人对局。

验证包含三种已识别的档案来源、过期和非法卡非零回包、原 PvP 列表、取消与重选；跨进程测试分别验证指定地图的房间交接和释放端口。隔离的零售客户端通过原生大厅按钮进入人机页面，在同一次匹配中创建 13 个 `PBPlaylistItemWidget`、将随机卡排在第一位，并在点击「OSS · 普通人机」后，由专服自身报告 `Map loaded: OSS` 和 `BP_PBGameMode_Rush_PVE_Normal`。详见 [协议回归](release-menu.test.cjs)、[卡片回归](verify.test.cjs) 与 [跨进程回归](../../src/core/runtime_ops/local_pve/auto_pool_tests.rs)。

同一次实机对局中，Windows 确认游戏只监听 `127.0.0.1:19788`。自动房产生 `join_ack` 后，客户端在 OSS 的原版选角页确认部署，读回已认领的侦察角色，间隔两次的 `IsAlive` 均为 1；结束后同一游戏端口释放。[脱敏验收结果](evidence/20261129-next-match.json)。其他 6 个组合有零售表与协议测试，没有逐图真机出生或三人联机验收。

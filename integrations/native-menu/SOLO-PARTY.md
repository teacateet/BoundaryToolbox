# 单人派对与原生地区菜单，2026-11-21

**结论：已复现并修正单人派对报错成员、跨账号读取状态；没有实机证据证明它就是地区列表为空的原因。** 原作地区摘要会调用 `IsPartyLeader`，但已发布的 `CreatePartyResponse` 和 `QueryPartyPresenceResponse` 都没有队长字段。验证到的是本人建队、本人为唯一成员，不是零售客户端已经判定为队长。

| 验收项 | 实际结果 |
| --- | --- |
| 旧服务对照 | 在 `b4d8f3b` 的三种托管来源，用不同测试账号正常登录，建队都得到了同一个样例成员。A 写入 `InShowRoom`，B 未写便读到该状态；B 写入 `InHall`，A 随之读回 `InHall`。 |
| 修后单人 | [托管准备器](../profile-runtime/prepare.cjs)只改已识别的单人模板。绑定前无队伍数据；绑定后，建队和状态回包只含该连接的账号。A、B 同时在线，队伍 ID 不同、状态独立；A 的连接经正常登录改绑 C 后从 `InMatching` 开始，B 不变。`Get` 仍为空，不新增晋升、邀请或队长字段。 |
| 已有多人 | 自带双人名单的合成来源返回 `Unsupported`，没有产生托管副本，源码不变。在原服务的回环副本，队长和成员分别登录，两端实收原成员顺序及各自状态。此项验证不改写已有队伍，不代表双设备组队实测。[协议回归](release-menu.test.cjs) |
| 地区 | 实物 DT 共 7 个行键，SHA-256 为 `0ecad9cc89e2f4f8df2859a6ce7485f5e3eabed2e5d3ad7105653a1247a38c96`。[预览实收回包](evidence/20261121-preview-region-reply.json)唯一一行以 `RegionName=us-east1` 命中，GUID 不命中；该行源文字为 South Carolina / North America。未交换名称与 ID，也未把 7 行都当作服务可用地区。单人建队的同一连接实际读到 3 档 PvE 和来源原有地区：发布版、本机各 1 行，社区 main 为 2 行。[实物表回归](region-acceptance.test.cjs) |
| 原生装备 | 复用 [native-equip.cjs](../loadout/native-equip.cjs) 和 [verify.cjs](../profile-runtime/verify.cjs)。三种来源均通过重新登录与服务重启后的主副武器、部件、挂件、角色外观和其他账号隔离；未改档案格式或玩家数据。 |

**零售证据与缺口。** [旧预览日志](../../target/player-preview/orbital_profile/login-server.log)前 289 行里，带游戏客户端标识的连接在第 35 行绑定了非样例账号，第 62、175 行仍发出地区查询，第 288 行断开。这段没有开始匹配，旧处理器也未记录建队回包或界面行数。本轮检查时没有零售游戏窗口，Windows 窗口工具未能初始化（`missing field sandboxPolicy`）；该游戏现有的 3 张 Steam 截图均为局内或靶场。未替换占用正式回环端口的旧预览服务，未操作第三方服务或游戏二进制。

**主线程现场验收：** 在含本修订的托管服务启动后，由正版 Steam 玩家单人进入「人机 → 普通」，记录是否出现「仅队长可选」提示；打开 `TERRITORY`，记录刷新后的可点行数，再选一行点 `CONFIRM` 核对摘要。用同次日志的 [region-window](menu-trace.cjs) 对照点击前后。隔离测试生成的入口 SHA-256 为 `702a44d5a0650d6b67d24f6f5143616c9e363946e9b1166b12c4a0a9558b3640`；已运行的旧预览入口为 `0e1728f7…`。

**复验：** 系统 Node `24.14.1`、游戏随附 Node `24.22.1` 各通过菜单回归 37/37，且各通过三来源的原生装备对照；系统 Node 的完整档案运行时回归退出码为 0。

```powershell
$table = 'C:/BrtWork/Assets/exports/tables/ProjectBoundary/Content/GameData/CSVConfig/Matchmaking/DT_MatchmakingRegionAddress.json'
node integrations/native-menu/region-keys.cjs $table integrations/native-menu/evidence/20261121-preview-region-reply.json
node --test integrations/native-menu/*.test.cjs
node integrations/profile-runtime/verify.cjs
node integrations/loadout/native-equip.cjs 'C:/BrtWork/SDK/sdk-fixture/validation-server'
```

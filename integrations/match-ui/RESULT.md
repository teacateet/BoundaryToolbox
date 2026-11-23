# 原版边境局内雷达、F2 设置与 ESC 选角

2026-11-21：原作局内菜单有可点击的设置和部署入口；现有证据不支持修改玩家 INI、角色雷达 ID 或 PvE 换队规则。现场仍缺按键、HUD 和出生角色的同局观察。以下诊断只读当前配置、既有原版资源导出和已结束对局的日志。没有启动游戏、连接正式档案端或改动房间。

## 原生菜单验收

| 项目 | 实际证据 | 父线程同局验收与判定 |
| --- | --- | --- |
| 雷达 | 18 个角色的雷达 ID 和类都匹配，资产查询返回全部 3 项。原作发行时的画面说明：左下雷达显示队友和被标记的对手；敌人接近告警另算。原版还分别提供敌人被标记、过热的图标和告警材质。 | 在同一角色、同一位置，先看面板和已知队友，再比较同一游戏内目标标记前后。分别记录面板、目标图标和接近提示。只有空场没有敌点不能判为雷达坏；若面板整块缺失，或已有明确标记但无图标，才分别查 HUD 或标记显示链。 |
| F2 设置 | 局内 `InGameOption_V2` 有 `Button_Setting.OnClicked` 和自己的 `OnKeyDown`；快捷键提示引用 `DT_InputActions.FieldModSetting`。本机键盘层有 0 条动作覆盖、2 条轴、0 次清空。现有明文菜单导出未包含该数据表的键盘行，无法证明实键就是 F2。 | 在已显示的局内选项页，先记下设置按钮显示的键帽，用鼠标点“设置”，再回到相同焦点按提示键；F2 若不同则单列。只有鼠标可用才优先查按键和焦点。设置页将 FOV 临时调到另一个可选值、应用、重开并核对落盘，再恢复原值。进菜单与保存设置分开判定。 |
| Esc 选角 | 菜单返回提示是 `Back`，角色列表的选择回调与点击“部署”的回调不同；部署使用 `DT_InputActions.Deploy`，另有带成功位和错误码的预订回调。Warehouse 普通 PvE 曾实际进入开局选角。 | 依次记录初次部署、存活时、正常等待重生时按 Esc 的页面。能进页后先选角色，再点“部署”，记录反馈、下一次实际出生角色；不要把列表高亮当成提交成功。开局能选而存活时不能，并不足以判键坏；能点部署却出生错误，应查部署/出生阶段。 |

- 雷达：原版 标记原因枚举 区分本地、视野、开火和热量；发行资源清单 能核对专用图标。键位页的 Mark 是“标记”，不是雷达总开关。`GameUserSettings.ini:77` 的 `MarkMethod=0` 是现值，没有依据将 0 解释成关闭雷达。原作玩法可对照 [Hygge 的同期实测](https://www.gamersky.com/handbook/202304/1585724.shtml)；[开发组年末更新](https://steamcommunity.com/app/1364020/allnews/) 单列了接近告警距离调整。两者都没有给出本版本的数值阈值。
- F2：原作菜单键提示、鼠标点击绑定 与 普通设置页的 Save 是三个不同证据。[CommonUI 的提示控件](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/CommonUI/UCommonActionWidget) 用于展示平台动作，不证明本局收到按键。BaseInput.ini:33 中的 F2 是引擎调试视图绑定，不是玩家设置键。已归档的项目 `DefaultInput.ini` 匹配片段中的 `CSTMBackToMenu` 对应自定义装备上下文，不能移植到局内选角。用户 INI 是 [分层覆盖](https://dev.epicgames.com/documentation/en-us/unreal-engine/configuration-files?application_version=4.27)，缺少一条本地记录不等于出厂动作缺失。当前 `Input.ini` 与 `GameUserSettings.ini` 均未设置文件只读属性。
- Esc：角色列表、预订回执 与 部署动作 可独立核对。游戏控制器另有 `CanSelectRole` 和 ServerConfirmRoleSelection，属于游戏服，不是 MetaServer 的角色档案 GET。角色列表行数、`bTeamChange=false`、保存档案均不能证明这一局允许或拒绝部署。

## 历史记录

- 9 月 19 日 Warehouse：第 14、15 行载入 Warehouse / Rush_PVE_Normal，第 54 行有玩家连接，67/70 行开始选角，143 行已有对局事件。9 月 28 日同模式 也有 101/104 行的选角阶段。两份是历史社区服务的阶段记录，不含某个玩家的按键、所选 ID 或下次出生 ID。社区项目也区分 [RoleSelection 与 InProgress](https://github.com/STanJK/ProjectRebound/blob/74c68ac3e6007c36248c442a281c2b7b831cc53a/Docs/ForAI/04-RE-Data.md#L205)。
- 11 月 19 日专服：Warehouse 在第 84 行开局、353 行结算，心跳计数峰值 6（不能据此区分真人、AI 或本机可见目标）；之后的 CircularX 段没有记录开局。同日档案日志：仅一次客户端连接，43 行 GET 在首次场景上报前，504 行 GET 在 492 行已上报返回 InHall 之后，506 行才进入 InShowRoom。两次回包都不是 Esc/出生的专属回执，且服务的 Presence 为进程级状态。

## 只读复核

```powershell
$win = 'C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64'
$menu = 'C:/BrtWork/SDK/_ui_stage/ui_export_2026-10-19/raw/ProjectBoundary/Content/UI/Menu_V2/Widgets/InGameOption'
& "$win/nodejs/node.exe" integrations/match-ui/inspect.cjs --menu $menu --archive-log 'C:/BrtWork/SDK/installer_tool/login-server.log' --server-log "$win/ServerLauncher/logs/log-20261119_175617.txt"
& "$win/nodejs/node.exe" --test integrations/match-ui/inspect.test.cjs
```

[检查器](./inspect.cjs) 只输出匹配的绑定、设置值、场景和行号；它不读取玩家装备文件，也不输出控制台历史或帐号。可用 `--config` 指定隔离的 WindowsClient 目录。若有同版明文原表，`--actions <DT_InputActions.json>` 配合 `--menu` 只解析正确表的 `KeyboardInputTypeInfo.Key`，不把手柄键当作键盘键；`--default-input <DefaultInput.ini>` 可另列出厂绑定。比较保存前已有的用户设置快照用 `--before-settings <GameUserSettings.ini>`，读不到两端数值则报告未知。以上均不写 INI、UMG 或服务。

2026-11-21 在随游戏分发的 Node.js 24.22.1 上运行以上检查及既有的 verify.cjs：7 项合成检查、1 项原作菜单资源检查通过。旧版与修正版的隔离 HTTP/RPC 回归均满足各自预期，18 个精确角色响应未变，历史两次 Sniper 读回均只有右挂载偏离默认。下列为保留的 11 月 20 日档案服务基线；本次未改动补丁或发布哈希。

## 11 月 20 日入口基线

- 工具箱以目标 `ProjectBoundary/Binaries/Win64` 为基准，选择其 `BoundaryMetaServer-main` 为工作目录，由随附 Node 执行 `index.js`。11 月 19 日启动日志记录了同一 Win64 下的 Node、ServerLauncher 和游戏。回归使用这一份服务。工具箱路径选择 · 启动调用 · 当次启动日志 · 启动包交接
- 当前配置为 `pve / normal / Warehouse`。11 月 19 日的服务器记录了 Warehouse 载入、`Rush_PVE_Normal`、开局（第 84 行）、结算（第 353 行），随后轮换并载入同模式的 CircularX（第 376、377 行）。服务端配置 · 当局服务器日志
- 模式表把 MiniFarm、CircularX、OSS 列入普通 PVE 的 `MapsForMatch`。Warehouse 的直接开局与这个匹配名单可以分别复核。14 个模式行都设 `bTeamChange=false`，这个字段描述换队。对局中的选角时机仍需角色选择画面和出生结果对应。启动包文档列出了地图、模式、难度与重启的正常服务器命令。模式表 · 启动包命令

## 三项核对

**雷达。** 18 个角色定义（其中 6 个标记 `bIsBot`）全部指定 `RadarID=11001001`，角色雷达类与该标准热探测器定义相同。雷达表另有 `11001002`（HS）与 `11001003`（LP）。定义索引的 HTTP 角色查询返回 `radarId`，隔离副本的正常 RPC `/assets.Assets/QueryAssets` 也实际返回全部 3 个物品。这里能确认角色装备数据和目录返回。目标条件、主动或被动触发、敌我提示和 HUD 绘制还需要同局画面。角色表 · 雷达表 · 定义索引 · 资产查询

**F2 局内设置。** 工具箱交接将 F2 列为待实测入口。Windows 客户端 `Input.ini` 的 `PBKeyboardInputSettings` 只保存两条 `Keyboard_MoveUp` 轴覆盖（SpaceBar / C）。第 5 行 `PrevWeapon=None` 位于手柄段。所查配置未提供 F2、Escape 或雷达的键盘动作映射。用户设置文件记录了 `FOV=80.000000`，可作为从正常菜单应用设置后的持久化对照。`CommonUI.ini`、`Game.ini` 均只有空白。菜单是否打开和设置是否写入是两个独立的观察点。交接验收项 · 键盘设置 · FOV 持久化值

**ESC 局内选角。** 普通档案读入口 `/assets.Assets/GetPlayerArchiveV2` 接受一组 `RoleIDs`。响应按请求顺序返回 `PlayerRoleDatas`，字段 1 至 9 包含角色 ID、装备及外观。11 月 19 日的两次回包序列都是 `PEACE, PROBE, Sniper, FORT, FIXER, SPIKE`。只比较六个装备槽与此版本的默认值，两次都只有 `RightPylon` 不同。两次读取至少在这个槽位保留了档案数据。该服务的 REST 角色详情路由供工具查询。当前记录无法把这两次 RPC 或任何 REST 访问与 ESC 的按下、角色确认或实际出生关联。请求 schema · 响应 schema · RPC 分派 · 当局两次回包，第 45、505 行

## 可选隔离补丁

原定义表键为 `SNIPER`。基线的 `GET /api/definitions/roles/SNIPER` 为 200，使用实战日志中的拼写 `/Sniper` 为 404。另一个可复现的条件是，档案保存为 `SNIPER` 而客户端请求 `Sniper`（或反向）。基线 RPC 在装备槽与嵌套外观、装备消息的两处读取都按键精确取值，大小写不同就回落到默认数据。角色定义路由 · 档案槽位读取 · 嵌套配置读取

补丁复用 MetaServer 的角色定义解析器。HTTP 返回定义表中的规范 ID。RPC 保留请求原有的 ID 拼写和顺序，先取档案精确键，只有已知角色且存在唯一的大小写别名时才取别名。两份不同拼写的记录并存时，各自的精确查询独立，第三种拼写继续使用基线默认值。改动只在隔离副本的两个读接口生效，写入逻辑、持久化格式和 protobuf 字段沿用本机版本。[可选补丁](role-read-case.patch)

## 回归与尚缺证据

在工具箱根目录运行以下命令，使用 Node.js 24.14.1、Git 2.53.0 与本机 MetaServer 已安装的依赖。可选的后两个位置参数依次为 MetaServer 目录、已有登录日志。第二条命令还会对照当局两次回包，只输出行号和偏离默认值的槽位名。

```powershell
node integrations/match-ui/verify.cjs
node integrations/match-ui/verify.cjs 'C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64/BoundaryMetaServer-main' 'C:/BrtWork/SDK/installer_tool/login-server.log'
```

验证器校验三个服务源文件的 SHA-256，在本目录生成临时副本，使用合成档案和随机本地回环端口运行相同的 HTTP 与 protobuf 读请求。基线与补丁的 18 份精确角色详情响应摘要一致。RPC 覆盖 9 个请求 ID，包含大小写双向查询、并存键、歧义键、缺省角色和未知角色。补丁后 `Sniper` 的 HTTP 为 200，唯一别名能读回保存的槽位与嵌套配置。精确键与缺省响应对照通过，合成档案前后字节相同，反向补丁检查通过。临时目录由验证器回收。[验证入口](verify.cjs) · [回环服务回归](replay.cjs)

本机 `Saved/Logs` 下只有空的 `NGX` 目录，Win64 的 `clientlogs/clientlog-20261113_141338.txt` 为 0 字节。三项实机验证由主线程在同一场正常对局记录地图、模式、角色、存活或等待出生状态与时间。CircularX 同时出现在匹配名单和服务器载入记录中，可作对照。

1. 雷达记录同一角色在有、无可见目标时的 HUD，并标出当时可从游戏内确认的目标阵营与状态。由画面建立触发条件。
2. F2 记录按键前后的画面、菜单标题、设置应用结果，以及重开菜单后的值。若选择 FOV 作对照，可同时只读核对 `GameUserSettings.ini` 的 `FOV` 行。现场值可与本次快照不同。
3. ESC 记录菜单实际出现的选项、可选角色、确认结果和下一次出生角色，分别标明存活与等待出生状态。用相同时间段的 RPC 记录核对数据读取。录屏或截图、设置落盘和出生结果各自回答不同的问题。

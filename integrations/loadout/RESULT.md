# 原版 Boundary 军械库写入与读回

## 角色更新的内层状态码

11 月 14 日记录把角色 V2 请求生成器、响应解析器和完成回调定位在同一任务虚表。外层成功且解析成功时，解析器置成功标记、复制内层状态，最后置完成标记。回调在普通委托分派槽读取这个状态，遇到零写为 404，再向装备 API 转发。该函数只检查状态本身。任务槽与 UE 的完成队列顺序相符，11 月记录保留了函数和虚表，具体玩家会话的调用次序仍属静态推断。请求类型 · 任务虚表 · 复制内层状态 · 委托分派 · [Epic 完成队列顺序](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/OnlineSubsystem/FOnlineAsyncTaskManager)

装备处理器收到 200 时更新本地角色数据、发出成功事件，收到 404 时映射到 UI 错误 4。社区已有内层零进入换枪错误 4、换配件回调正常的客户端记录。托管服务本次仅调整角色提交成功的内层码，角色拒绝为 1，外层通信成功为 0，配件成功为 0。处理器分支 · 社区客户端对照 · [托管服务](./native-equip-appearance.patch:91)

合成 Sniper 副武器重放在旧版 19.20.21 读到内层 0，按新契约断言时复现失败。修订版 V0.8.4 托管入口 SHA-256 为 `96a690fea8ee6bdc8bcbc096ca4895857f3da18ee909c9fdd2fee979a92b5af2`，四次莫辛写入均回 200，同会话和重启的 RPC、整档及逐角色 REST 均读回莫辛。三个受支持来源均通过提交与拒绝断言。玩家界面验收仍待实际画面。[Mosin 回归](./sniper-secondary.cjs) · [拒绝与磁盘校验](./native-equip.cjs) · [集成测试](../profile-runtime/verify.cjs)

## 19.20.21 玩家换枪页仍显示旧副武器

玩家确认是狙击手的副武器，选莫辛后仍在当前换枪页看到旧枪，不需要进局才复现。实际 19.20.21 服务日志的第 45、64 行在点击前返回主 GSW-PSR、副 RU-SVD；第 78、81、98、101 行收到 `op=2 role=Sniper item=SNIPER_RU-MOSIN` 并各有保存记录。同一服务指向的存档副槽为莫辛。从第一次点击到离开页面、返回大厅并断开，这段日志没有新的角色档案读取。不能由此判断客户端是否消费了写入应答，或把“已装备”提示当作界面刷新。

[针对这一操作的隔离重放](./sniper-secondary.cjs)使用玩家实际运行的托管应用代码副本、单独端口和全新合成档案；没有读取或复制玩家档案。先还原点击前的两个槽位，再连续提交四次，按实际顺序读取六名角色。运行入口 SHA-256 为 `702a44d5a0650d6b67d24f6f5143616c9e363946e9b1166b12c4a0a9558b3640`。四次应答均为成功，主动回读和重启回读均为副武器莫辛，主武器没变。这个结果排除了这组输入在服务端读回旧枪，**没有修复或验证游戏换枪页面**。

### 客户端刷新与 REST 读回

玩家会话只在四次提交前请求了一次整档 GET（第 39、40 行），最后一次写入到断开（第 101 至 116 行）未再次请求整档、逐角色或原生角色档案。[玩家会话](../../target/player-preview/19.20.21/player/orbital_profile/login-server.log#L39)

社区标签 V0.8.4 在首次大厅主菜单构造时调用 `FetchAndLog`，读取整档 `/api/loadout/<player>`。全量刷新按显示角色的 `CharacterID` 精确查 `roles[charName]`，分别从 `primaryWeapon`、`secondaryWeapon` 更新角色模型。更新编辑面板时，它按 `EditingCharacterID` 找同一角色，却把每个面板的 `EquippedInventoryID` 都设为主武器。该函数的 F8 注释缺少可核实的玩家端绑定，同标签 Payload 的业务 C++ 调用点里只找到服端的 F8 分支。当前记录无法确认玩家点击时执行过全量刷新，也无法把这行主武器赋值认作显示旧副枪 SVD 的原因。[首次读取](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/Hooks/Hooks.cpp#L689-L696) · [角色模型](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/Loadout/LoadoutFix.cpp#L248-L261) · [面板赋值](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/Loadout/LoadoutFix.cpp#L281-L295) · [F8 条件](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/Hooks/Hooks.cpp#L447-L453)

使用两个玩家版本的服务代码副本和合成账号，分别在写入前、四次写入后、重启重登后主动读取整档 `roles.Sniper` 和逐角色 `/Sniper`。两条路由均返回 HTTP 200，主武器保持 `SNIPER_GSW-PSR`，副武器依次为 `SNIPER_RU-SVD`、`SNIPER_RU-MOSIN`、`SNIPER_RU-MOSIN`。19.20.21 的入口 SHA-256 为 `702a44d5a0650d6b67d24f6f5143616c9e363946e9b1166b12c4a0a9558b3640`，19.20.22 为 `75929cb80588950b61f54807434b365649d565190a5f1b75a045f00f36e0db9f`。两次回执均为 `fullAndRoleRestVerified=true`、`gameUiVerified=false`。[隔离回归](./sniper-secondary.cjs#L29)

## 2026-11-21 大厅换枪与外观

主线的 `profile-runtime/prepare.cjs` 先部署存储投影，再依次应用角色键、REST 和菜单补丁。军械补丁以这个组合为基底，保留 `loadoutSnapshot.weaponConfigs`，7 个 hunk 的旧文本均在对应目标中唯一。V0.8.4 发布包 SHA-256 为 `fff143b06c2782d559a19c86d32979c09fdeaa29afcccc99a16637c70fc16094`。本次军械补丁 SHA-256 为 `7798ce14d4ba8e274e6aecf29010b297889b190580d59f7141eab630855bf2cc`。[军械补丁](./native-equip-appearance.patch) · [发行组合复验](./fixture-kit.cjs)

| 原生操作 | 同一托管服务关闭本补丁 | 启用本补丁 |
| --- | --- | --- |
| PEACE 先按角色装备 ID 选主 RU-SVD、副 GSW-DMR | 两个非默认 ID 正确 | 相同 |
| 再按基础 ID 选主 GSW-DMR、副 RU-SVD | 回执为 0，两个槽仍为原顺序 | 回读 `PEACE_GSW-DMR`、`PEACE_RU-SVD` |
| 对基础 ID `GSW-DMR` 保存武器挂件 | 已装备枪回读 `WO-NONE` | 回读 `WO-SUN`，换下再装备仍保留 |
| PROBE 保存臂章、头饰 | 两个字段均为空 | `SkinConfig.SpecialSlot=ABGNoEntry`、`Ornament=HOGoatHorn`，可单独卸下 |
| PEACE 换非默认服装，随后明确清空涂装 | 服装已保存，旧涂装重新出现 | 保留 `PEACE_TIGER`，涂装为空 |

历史大厅日志在第 150、154、158 行记录了 `op=10, PROBE, HOGoatHorn`，服务三次记为忽略。第 162 行提交角色金色涂装，第 197 行回包同时含该涂装和该枪的 `WO-SUN`。较早的会话有 `op=2, PEACE_RU-SVD` 及 `PEACE_GSW-DMR`。公开服务源把 `op=1/2` 分别映射至主、副武器，把 `op=7`、`9`、`10` 分别映射至服装/涂装、臂章、头饰。基础武器 ID 的故障来自按发行定义构造的 A/B 复现。玩家最近一次换枪使用的具体 ID 仍待同一点击的日志核对。[头饰记录](../../target/player-preview/orbital_profile/login-server.log:150) · [原会话回读](../../target/player-preview/orbital_profile/login-server.log:197) · 历史换枪 · [公开槽位与回包](https://github.com/STanJK/ProjectRebound/blob/74c68ac3e6007c36248c442a281c2b7b831cc53a/Metaserver/index.js#L387-L425)

武器 ID 用所选角色的 `WeaponScope` 和 `DT_RawItemType` 重定向表取唯一匹配。逐枪档案和主副槽使用同一个角色装备 ID。角色服装保留明确设置和省略字段的区别，兼容已公布的旧款 `PEACE_TOK-DOS-01` 原色组合。角色和枪各自保存外观。被拒绝的装备请求返回非零，合成档案字节保持一致。对于旧档的同枪基础键和精确角色键，回读优先使用精确键。基础武器与角色映射 · [独立读方协议](./native-read.proto)

已通过三个托管来源的正常 HTTP 登录和 TCP 档案读写，覆盖 V0.8.4、独立 MetaServer `007850d0` 和本机 `3961c933`。回归用合成账号、临时回环端口，比较六个角色、两把枪、两个账号、新会话、服务重启和磁盘字节。图形验收还需在同一次军械库操作关联界面选择、档案应答及进局后角色实际佩戴和持有的物品。[运行结果](./native-equip-proof.json) · [原生装备回归](./native-equip.cjs)

已在一次性仓库副本用原样的 `node integrations/profile-runtime/verify.cjs` 跑完主线组合，退出码 `0`。V0.8.4 通过首次保存和重启后的原生 RPC / REST 读回。已有协议断言覆盖主副武器、两个弹匣、左右挂载和角色键。带三层、四层旧补丁的输入也生成了相同的托管应用。另用游戏自带 Node `v24.22.1` 重跑三个来源的 `native-equip.cjs`，均通过。测试使用发行包、临时端口和合成档案。[主线验证入口](../profile-runtime/verify.cjs)

从仓库根目录运行以下命令。最后一个参数是已有源码、定义及依赖的只读来源，脚本只在临时副本写入。可用 `PROFILE_TEST_RELEASE`、`PROFILE_TEST_MAIN` 和 `PROFILE_TEST_MAIN_MODULES` 指定同版本的本地测试输入。

```powershell
$source = 'C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64/BoundaryMetaServer-main'
node integrations/loadout/fixture-kit.cjs
node integrations/loadout/native-equip.cjs $source
node integrations/loadout/native-lobby.cjs $source
node integrations/loadout/verify.cjs $source
```

## 2026-11-20 存储投影

2026-11-20，使用游戏自带的 Node.js v24.22.1，在临时档案端副本上完成 HTTP、TCP RPC、磁盘和服务重启回归。正式目录只用于读取服务代码、定义、协议和依赖，状态为 `ready`。改动及实测记录均位于 `C:/BrtWork/BRT/integrations/loadout/`。完整输出见 [verification.json](./verification.json)。

## 复现

同一名隔离玩家先通过 REST 保存 PEACE 的结构化配装。请求明确指定 GSW-AR 为主武器、RU-AKM 为副武器，并故意先列出副武器的配件。随后通过正常游戏会话卸下左挂载、把右侧换成 ATK-HE，并把 GSW-AR 换回标准配件。

| 读回点 | 现行版本的隔离副本 | 修正后的隔离副本 |
| --- | --- | --- |
| 首次原版角色档案，主 / 副 | RU-AKM / GSW-AR | GSW-AR / RU-AKM |
| 首次原版逐枪档案，GSW-AR / RU-AKM | STD / STD | EXT / DRUM |
| 游戏再次改装，原版 RPC / 角色 REST | STD / EXT | STD / STD |
| 游戏卸左换右后，原版角色档案 | None / ATK-HE | None / ATK-HE |
| 同时读取 REST 的左 / 右挂载 | ATK-HE / TAC-EMP | None / ATK-HE |

原版装备 RPC 把主副武器和左右挂点分别写入角色槽位，把配件按武器 ID 写入 protobuf 档案。REST 却把结构化快照原样返回，并从 `weaponConfigs` 的键顺序推断主副武器。REST 写入的配件也只停留在快照。问题覆盖两个方向。正式端入口可在 index.js 和 loadoutStore.js 核对。

修正后的服务用已装备槽位和逐枪档案生成角色与整档 REST 视图。REST 局部写入会合并指定配件槽，校验角色和配件定义，保留其他武器、挂点和角色快照字段。单独改装一把备用武器时保留已装备的主副武器。仅在新档首次收到旧格式且只靠多把武器的键表示顺序时，沿用旧格式的主副推断。旧结构化档案在加载时补齐内存中的逐枪索引，第一次显式保存才落盘。安装和回滚只替换服务端 `game/loadoutStore.js`。实现见 [projection.cjs](./projection.cjs) 和 [deploy.cjs](./deploy.cjs)。

## 主线程接入

以下 PowerShell 命令可从任意目录执行。工具箱可以直接用参数数组启动同一个 Node 进程，并解析标准输出中的 JSON。

```powershell
$integration = 'C:/BrtWork/BRT/integrations/loadout'
$win64 = 'C:/BrtWork/SteamLibrary/steamapps/common/Boundary/ProjectBoundary/Binaries/Win64'
$server = Join-Path $win64 'BoundaryMetaServer-main'
$node = Join-Path $win64 'nodejs/node.exe'
& $node "$integration/deploy.cjs" status $server
& $node "$integration/verify.cjs" $server
```

打包时将 `deploy.cjs` 和 `projection.cjs` 放在同一目录。在现有启动流程创建档案服务进程之前执行 `node deploy.cjs apply <server>`。成功返回 `state: "installed"`，重复执行也返回该状态。下一次启动 `index.js` 即加载新代码。服务重装后可再次运行 `status`。兼容性根据存储模块、服务入口、定义索引和三个协议文件的 SHA-256 判定。版本不匹配时 `status` 退出码为 2，写入或回滚失败时退出码为 1，错误信息输出到标准错误。回滚命令为 `node deploy.cjs rollback <server>`，原件保存在 `game/loadoutStore.js.boundary-toolbox-v1.orig`。

工具箱从现有本地 HTTP 服务查询 `GET /api/definitions/roles/{roleId}` 和 `GET /api/definitions/weapons/{baseWeaponId}`。读取选定角色用 `GET /api/loadout/{playerId}/{roleId}`，整档用 `GET /api/loadout/{playerId}`。保存用 `PUT /api/loadout/{playerId}`，只提交本次编辑的角色和槽位，避免并行编辑覆盖已更新的字段。下面是一个单角色请求体。

```json
{
  "roles": {
    "PEACE": {
      "leftPylon": "None",
      "rightPylon": "PEACE_ATK-HE",
      "weaponConfigs": {
        "PEACE_GSW-AR": {
          "parts": [{ "slotType": 9, "weaponPartId": "GSW-AR_MAG-AR-EXT" }]
        }
      }
    }
  }
}
```

选主副武器时显式发送 `primaryWeapon` 和 `secondaryWeapon`。若旧档已经把两个槽位存反，读回沿用角色当前保存的选择；一次显式提交两个槽位即可纠正。确认写入应检查 HTTP 状态、`validation` 和随后一次角色读回。正式档案端由主线程按需部署，本次执行过的 `apply` 和 `rollback` 均针对隔离副本。

## 实测与验收

- [verify.cjs](./verify.cjs) 对同一组输入分别运行基线与修正版。7 项跨接口断言在基线全部复现偏差，在修正版全部通过。临时服务仅复制白名单代码和游戏定义，在新目录写入两个合成玩家的档案，完成后关闭子进程并清理目录。
- 测试通过 `/connectServer` 获得服务签发的测试会话，再调用 `UpdateRoleArchiveV2`（主、副、左、右）、`UpdateWeaponArchiveV2` 和 `GetPlayerArchiveV2`。响应由[独立读方协议](./native-read.proto)解码。角色 REST 和整档 REST 均与原版读回核对。
- 重启服务并重新登录后，原版 RPC 与两条 REST 响应和重启前一致。还核对了另一位玩家写入隔离、错误槽位导致整次请求撤销、旧快照重新装备仍保留配件、只读前后磁盘字节相同、重复安装和回滚，以及遇到第三方代码变动时保留该文件。复跑时加 `--receipt` 可在本目录刷新验证 JSON。
- 进局侧可静态追踪到角色预选装备刷新和包含两把武器及左右挂载的角色配置。实际运行验证止于档案端应答。游戏内界面、进局首次出生和重生后的实际装备仍待图形验收。对应数据结构见 ProjectBoundary_structs.hpp。
- 图形验收使用独立测试身份。在军械库设定主副武器和不同配件，卸下左侧并更换右侧，退出军械库再进入。随后进入对局，逐项比对主副武器、两组配件及左右挂载，再重生一次复核。

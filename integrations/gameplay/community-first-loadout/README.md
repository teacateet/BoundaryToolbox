# 同一角色只发一次装备

构建开关为 `-IncludeFirstLoadout`。live38 修复了重复发装。live39 还处理了武器已进入背包、首命双手仍为空的情况。

在普通难度 CircularX 的零售客户端重现时，PEACE 的第一人称镜头已回到存活角色。客户端背包有两件武器，但持续超过 15 秒没有当前武器或待装备武器，双手处于 EmptyHand。在同一条命调用原生装备动作后，客户端将 AKM 挂在左手的武器插槽，画面中出现枪身。组合候选在另一场首命也拍到了 AKM。

[FirstEquip.hpp](FirstEquip.hpp) 仅在已验证的本机 PvE 世界、第一条命、回合开始、本人活动镜头下处理。背包第一个武器的所有者必须已同步，未出现切枪、挂载或其他双手动作，持续空手 800 毫秒后才请求装备一次。后续死亡或换角色会结束检查。[首命回归](tests/first-equip.cpp) · [只读截图与网格检查](tests/observe-first-person.cjs)

## 现象

每局第一条命不正常：玩家看到只有手臂、什么都用不了，死一次之后恢复。live36、live37 的客户端日志里，首命的机动装备都记成 `module=foreign`（装备的 `OwnerCharacter` 不是本人角色）。

## 原因

以下均为 Build 14530908 的 RVA，CircularX、Rush_PVE_Normal 联调所见。

1. 确认干员后社区服调 `StartMatch`，原生立刻出生角色并发整套装备，此时 `RoundState=InvalidState`。这一次武器会正常上手。
2. 5 秒后 `RoundState` 进 `CountdownToStart`，原生回合流程对**同一个角色**再占有一次。占有处理里 `0x1584F86`、`0x158509E` 两处调用 `0x15851A0`，它把配装数据抄进角色 `+0x580`，在服务端按类别逐件生成装备，不清理旧件。
3. 结果：`Inventory` 里两把主枪两把副枪，角色身上两个钩索模块，当前机动装备指向后生成的那个，它的 `OwnerCharacter` 为空（服务端、客户端都是）。
4. 上游原版在开场镜头结束后还会走一次 `RestartPlayers`，同一角色变成三套（`Inventory=[6]`）。现用的 [first-life](../first-life/README.md) 补丁去掉了这一步，但第 2 步的重复仍在。

## 修法

在 `0x15851A0` 上加一个服务端内联钩子：角色已经有武器、机动装备、挂载或近战时直接返回，否则走原函数。实现见 [main.patch](main.patch) 里的 `BrtBuildLoadout`。

联调结果（画面）：首命 `Inventory=[2]`，`CurrentWeapon` 是主枪，钩索模块的 `OwnerCharacter` 是本人角色，客户端画面有枪、有挂载和钩索图标。倒计时那次只触发 `NotifyBorn`，不再有第二次 `K2_InventorySpawned`。

## 试过但不成立的做法

- 把首次出生推迟到倒计时（`PlayerCanRestart` 在 `InvalidState`、`RoleSelection` 返回假）：只出生一次，但武器始终不上手，`CurrentWeapon=null`，画面连手臂都没有。之后再强制 `RestartPlayer` 也不上手。
- 只去掉倒计时期间每帧一次的 `Possess`：对机动装备归属没有影响。

## 验证范围

首命画面和档案读回用本机普通难度 PvE 验证。[live38 的配件和挂载测试](../../../docs/BRT-LIVE38.md) 另有记录。多回合模式未在这次联调中覆盖。

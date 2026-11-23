# 钩索的游戏源码调用点

`APBCatchingModule` 的拥有客户端负责采集输入。房间服务器负责处理目标、运动、钩头回收、冷却和下次使用。三个具名 RPC 使用该游戏 Actor 的拥有连接。保留原生 `ServerLaunchHook_Validate` 和 `ServerLaunchHook_Implementation`。本地用各自独立的房主和玩家对象编译运行了这些方法。[游戏模块实现](Source/GrappleModule.inl) · [状态机](Source/BrtGrappleCycle.h) · [具名 RPC 声明](Source/GrappleModule.declarations.inc)

| 时机 | 执行条件与接线 |
| --- | --- |
| 装备、新生命、死亡、换装、关卡退出 | 生成器在原有 `APBCatchingModule::Tick` 之后调用 `BrtObserveNativeLifecycle`，核对 `HasAuthority`、当前 `OwnerCharacter`、`IsAlive` 和当前装备实例。原 `EndPlay` 之前清理计时器。游戏复用同一个 Pawn 且在两次帧更新之间完成死亡与重生时，在最终死亡回调额外调用 `BrtEndServerLife()`。 |
| 按下和松开 | 生成器识别现有 `APBCharacter` 对 `ActivateMobility` 的 UE `BindAction`，在原处理开头分别调用 `BrtOwnerPress` 或 `BrtOwnerRelease`。它保留原有键位与处理顺序。动作名通过常量、映射表或手柄分步分派的开发分支，需要在同一输入处理入口手工接这两项。 |
| 发射目标和牵引 | 生成器把 `APBCatchingModule` 自身成员中的独立原发射请求接到 `BrtOwnerLaunchTarget`。客户端一并传递这一生命与用次的序号和原 `FPBCaughtData`。服务器先跑原 `ServerLaunchHook_Validate`，核对当前装备、生命、原生 `Ready`、有效输入和冷却，再运行原发射实现。任一后续游戏规则判定目标无效时，在原生失败分支调用 `BrtNativeLaunchFailed(Use)`。原生飞行工作在发射时捕获一次 `BrtNativeLaunchUse()`，后续异步工作使用同一序号。 |
| 锁定及回收 | `Tick` 读取服务器 `CurrentHookStatus` 的 `Launching`、`Locked` 和 `Recycling`。原生物理回调可在写入各阶段后通过 `BrtGrappleGame::Locked`、`Recycle` 和 `Returned` 执行原施力、回收及冷却开始通知，每一项对同一发只执行一次。`SnapForce`、`MaxSnappingSpeed` 和锚点归原有物理处理。 |
| 钩头实际回到原生 `Ready` | 只有上一阶段已确认 `Recycling` 的该次使用才能进入 `Cooling`。模块读取 `DataConfig.HookCoolDown`，当前零售值为 8 秒，安排房主的 `FTimerManager`。原生分支已有计时器时，在开发工程中将它替换成上述唯一服务器计时，保留原冷却开始通知。零售蓝图 `Event_OnHookRecycleFinished(SERVER)` 转发给多播后只恢复钩头骨骼可见性。 |
| 到期和下一次使用 | `BrtCooldownTimer` 校验拥有者、装备实例、同一生命和用次，并对照服务器时间。它释放该次 activation latch，清除 `bIsInCoolDown`，发原 `ClientEndHookLockCoolDown()` 通知，再发给拥有者下一张可用序号。仍在按住的键要松开，新的按下沿才会消耗下一张序号。 |

`prepare_grapple_source.py` 读取真正 `.uproject` 中的游戏类声明，生成 Git 补丁。它保留现有 Tick 和 EndPlay 的函数体，并添加 UHT 可直接读取的 RPC。生成器在小型源项目上通过 `git apply --check`，输入绑定、发射调用与 Tick 已验证能通过正确职责的入口。原游戏项目目前缺失，UHT/UBT、实际分支的输入绑定形式、同步失败分支以及冷却计时器归并仍待在该工程核对。[源码补丁生成器](prepare_grapple_source.py) · [宿主回调](Source/GrappleGameCallbacks.h)

宿主生命 1 的用次 1、2 均命中有效实体并执行受力，位置从 0 到 200，再到 400 cm。第一次在 2.000 秒完成回收，房主拒绝 9.999 秒的提前通知，10.000 秒放行。测试也覆盖长按、重复发射、旧计时器、死亡重生、换装、非实体和超距目标。线下源工程目前没有零售客户端或专服的这次牵引回执。[钩索验证记录](grapple-verification.json)

原生钩索可能在两个 Actor Tick 之间完成锁定或回收。已观察到本发飞行、锁定或回收后，再看到原生待命即可启动一次冷却；若连飞行都未采到，仅接受携带当前用次的原生收回回调。刚提交发射时采到待命不算收回。最先到达的原生锁定、回收回调也能分别执行自己的原受力、回收操作。修订前 5 种跨帧顺序覆盖不到冷却或物理回调，修订后同生命的下一次新按键均能再次进入原发射函数；冷却期间的按键和上一发的迟到回调仍无效。[可编译回归](tests/GrappleHost.cpp) · 跨帧实测范围。此源码不在玩家 19.20.33 中。

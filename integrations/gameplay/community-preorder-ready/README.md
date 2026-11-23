# 登录预订等待原生角色表

本补丁处理 `live34` 原版普通局的两次同源崩溃：`PostLogin → BrtPreorderOwnerInventory → ServerPreOrderInventory` 在玩家角色表没有可用键时进入游戏未检查查找失败的路径，随后读取 `-0x18`。它不代表完整配装、挂载、钩索、选角或雷达已通过实局验收。

应用顺序：完整社区 main `74c68ac3`，原有 12 项社区补丁，出生 trace，然后本目录 `main.patch`。将 `PreorderGate.hpp`、`NativeRoleTable.hpp`、`PreorderReady.hpp` 复制到 `ProjectReboundMainDLL/BrtInventory`。补丁只修改已有 `OwnerLogin.cpp`，没有新增编译单元或事件 hook。

登录仍读取本人配装、绑定出生观察，但把六名干员的预订排队。已有游戏 Tick 只有在以下条件同时成立时提交：

- 原登录的控制器、连接、玩家状态弱身份仍有效，并匹配认证的房间成员。
- `PBPlayerState` 与当前 `PlayerState` 一致，两个原生 loaded 位均为 true。没有声称从本次 dump 读到了这两个位的值。
- 游戏模块的 PE 时间戳为 `0x6656d8d7`、映像大小为 `0x0648c000`，两处实际取表/查键的指令指纹一致。
- 通过本进程 `ReadProcessMemory` 只读复制 `PBPlayerState + 0x6C0` 的 POD 表头、角色键和库存数组表头，确认数组数量、容量和可读性均合法。每次 RPC 前重新确认该角色实际存在，并重新解析控制器、状态和连接弱身份。

`+0x6C0` 是本次原生调用查询的表。没有用 `ClientInitFieldMod` 的另一份装备快照替代它，也没有写入私有偏移。私有视图只含指针和整数，不复制、释放或构造游戏的拥有型 `TArray`。指纹不符则取消预订，不尝试猜偏移。

每个角色的预订调用正常返回后便记为已提交；这不等于游戏确认接受了装备。缺少角色键时继续等待，已经提交的角色不会重发。明确空槽仍按原配装提交。玩家已选角、连接/状态改变或达到 60 秒等待上限时取消。60 秒仅是放弃上限，绝不是允许提交的定时器。等待期间不重发 HTTP 读档。

诊断写入进程当前工作目录的 `logs/BRT-preorder-<PID>.log`，最多 32 行，仅含排队、变化后的等待原因、取消或提交结果，不含账号、令牌、物品或内存地址。现有 main 会将工作目录设为 Win64，因此不能假定此文件位于独立房间目录中。

## 验证范围

`verify.ps1 -OutputDirectory <临时目录>` 直接编译生产 gate 和 POD reader：15 个预订场景、12 个原生表读取场景通过。覆盖空表不提交、loaded 位未就绪、角色逐步出现、提交一次、选角/断开/换状态、重复键、数组不一致、版本和指令指纹不符、不可读地址。reader 用先包含 Windows 头的真实顺序编译，覆盖 `max` 宏冲突。`main.patch` 对实际 `live34` 组合源码的 `git apply --check` 通过。

这些是针对确定崩溃条件的回归，不是普通局通过的证明。完整 DLL 构建和下一场原版 Steam 普通局仍由维护侧分别记录。

# 普通本机 PvE 出生调用记录

这是只读诊断补丁，不更改配装、返回值、角色、库存或玩家档案。将 `BirthTrace.hpp` 复制到社区模块的 `BrtInventory` 目录，再应用对应的 `main.patch` 或 `v084.patch`。主线目录为 `ProjectReboundMainDLL`，v0.8.4 目录为 `Payload`；先应用已有出生及房间 HTTP 补丁。

只在 `RoomHttp::FromLocalServer()` 成功读取本进程的 `BRT_LOCAL_SPAWN_ROOM` 和 `BRT_LOCAL_SPAWN_TOKEN` 时启用，不发送 HTTP，不输出环境值。普通 Steam 客户端不启用这份服务端记录。

精确识别 `Function ProjectBoundary.PBFieldModManager.OnRestartInStartSpot` 与 `Function ProjectBoundary.PBFieldModManager.GetWeaponNetworkConfig`，以及 `ServerConfirmRoleSelection`、`ClientInitFieldMod`、`AddWeapon`、`InitWeapon`、`NotifyBorn` 的对应原生类完整名。`OnRestartInStartSpot` 的 `InController` 必须为具有服务器权限、PlayerState 和对应 NetConnection 的真实玩家，才建立拥有者上下文。管理器相同且仍在同步调用栈内的配置查询才继承该拥有者；上下文在栈展开时恢复。

日志前缀为 `[BRT-BIRTH-TRACE]`，每进程最多 100 行，最后一行为限额提示。`owner` 是进程内匿名编号，`call`、`parent` 和 `depth` 描述记录到的同步嵌套关系；`parts` 与 `slots` 只报告配置数组长度。没有账号、令牌、玩家名、地址或物品名。无拥有者的配置查询最多记录 4 次。

同一份日志同时写到 stdout 和专服工作目录下的 `logs/BRT-birth-trace-<PID>.log`，每行 flush。工具箱的本机普通房使用独立 `BRT-Auto-<难度>-<地图>-<端口>` 目录，因此文件落在该房间的 `logs` 内；不依赖旧启动器的 stdout 白名单。目录或文件不能创建、写入失败时不影响游戏。

`armed` 说明环境允许记录；`enter` 表示进入 hook；只有原 `ProcessEvent.call` 返回后才记录 `native-return`；`exit returned=0` 不能当作原生调用已执行。采样时临时抑制采样自身触发的重入，原游戏调用期间继续观察嵌套事件。析构只读取保存的数字，不再次读取可能被原调用销毁的实体。

没有记录某个事件，不能证明原生函数没有执行：原生代码可能直接调用 C++ 实现而不经过 `ProcessEvent`，也可能在另一调用栈执行或发生在记录限额之后。本补丁用于下一场正常对局一次性辨认可见调用链，不以日志代替局内配装验收。

`main.patch` 对应完整主线 `74c68ac3` 的 `Hooking/ServerEventHooks.cpp`。`v084.patch` 使用本机保留的 `Deprecated/PayloadOriginal/Hooks/Hooks.cpp` 相同 hook 上下文编写；本轮实际 DLL 构建采用主线，未声称 v0.8.4 已构建或运行。

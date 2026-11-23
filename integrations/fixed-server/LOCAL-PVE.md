# 工具箱自己的本机 PvE 房间

工具箱将 [local-pve-host.cjs](local-pve-host.cjs) 嵌入 BRT。它只用于原版大厅里由工具箱创建的本机单人 PvE。无需另装启动器，不覆盖社区启动器，固定房与远端加入仍走各自原来的路径。

[开房入口](../../src/core/runtime_ops/local_pve/auto_room.rs) 从当前模式数据表选地图并锁定 BRT 的独立房间目录。[房间管理](../../src/core/runtime_ops/local_pve/auto_pool.rs) 把子进程加入本房的 Windows 作业后才放行。安装已有的 Node 启动所选安装的正版专服，向专服传入与客户端相同的本机档案地址。它不冒充玩家、不代发出生读取。游戏端口的进程归属、原服实际输出的地图、模式和监听成功必须全部一致，工具箱才把房间交给大厅。日志只抄录就绪与回合事件；同图重载、换图、回合结束会使旧阶段失效。

本机开房前最后一次检查也按当前零售版的难度和地图配对：简单是 Warehouse、DataCenter，普通是 MiniFarm、CircularX、OSS，困难是 OSS、CircularX。房间目录或安装中的模式表若意外要求普通 Warehouse 等不支持的组合，专服不会启动。

初始选角和每次出生的数据接口已按本房签名、当前本机账号、连接、生命编号和选择做隔离。只有具备权限的接收端真正请求并取得这次的回包，首页才可显示「档案已发出出生配装」。本状态不证明角色已装上配件。普通固定房仍可显式提供自己的配置。

从维护工程根目录可做两项独立检查：

```powershell
node --test integrations/fixed-server/local-pve-host.test.cjs
$env:BOUNDARY_QA_WIN64 = '<已安装游戏的 Win64 目录>'
cargo test --locked --offline --release installed_empty_room_receives_scoped_launch -- --ignored --nocapture
```

第二项用新建的房间目录、临时游戏端口和独立的 Windows 会话签名记录；实际启动一台空的普通 MiniFarm，逐项核对正版专服参数、独占房间、UDP 归属和退出后的端口释放。测试不会启动玩家客户端或改写既有房间。它不能证明零售客户端的出生、钩索或 HUD 已修复。

# 本机 PvE 客户端交接 v1

Rust 为本次大厅独占监听 `127.0.0.1` 动态 TCP 端口。探测大厅命名管道成功后，在本次托管租约发布 `joinIpc: {v:1, port, nonce}`。nonce 是 32 个随机字节的 64 位小写十六进制表示。

一条 TCP 连接处理一张票据。每帧是一行以 LF 结尾的 UTF-8 JSON，最大 1024 字节，等待响应的期限为一秒。下表列出每行的全部字段。

| 方向 | op | 字段 |
| --- | --- | --- |
| Node → Rust | `offer` | `v:1`、`op`、`runId`、`requestId`、`clientPort`、`rpcPort`、`gamePort` |
| Rust → Node | `ready` | `op`、`requestId`、`nonce` |
| Node → Rust | `commit` | `op`、`requestId`、`nonce` |

Node 在正常登录会话的本次点击、选区及票据 Query 通过校验并写回分配后发起握手。`requestId` 是新的 UUID v4。三个端口依次来自客户端 RPC 连接的对端、本端及本次房间租约。Rust 核对两个连接的进程归属，并向本次大厅的管道发送 ping。Node 收齐 ready 后核对凭证、代次、当前票据、会话和房间。取消、返回大厅、重新绑定、断线和下次点击会撤销待确认的交接。

Rust 收到 commit 后再次核对租约、房间和 RPC 两端的进程归属，向同一大厅发送一次公开的 join。Node 的 `join-requested` 记录表示已写出 commit。实际进局由对局服同场玩家加入记录和玩家画面确认。

## 自动准备

### 匹配反馈（待打包）

只有两端均为 IPv4 回环（含 IPv4 映射地址）的大厅 RPC 才读取本机房间租约；LAN 连接保持原匹配路径。自动准备成功连到控制端后发送一次建票进度。正式票必须同时满足进度写成功、同一选择仍有效、已收到对应 prepared 和租约未过期；写失败会撤销本次票据，不主动重放。进度写成功不表示客户端收到票。原有 offer 仅无返回且未提交时重试一次；提交后的不确定结果不重发。

首页“进入原版大厅”按钮下和会话日志分别显示本机房间准备、客户端用本次票号 Query、桥收到 join_ack。最后一项只是“已交接，进房未核验”。Stop、回大厅、准备中断、写票失败和租约结束不当作成功。读取本次托管租约和启动后日志的工作线程不发网络请求；没有选择编号的桥记录若与同秒重选冲突，不用于确认新选择。局域网直接加入不使用这些本机阶段。

回归：`node --test integrations/native-menu/*.test.cjs`；`cargo test --locked --offline --all-targets`。服务端时序和界面绘制检查不能代替原生画面。打包后在同一次冷启动中连续录下点击、失败提示有无、切图及实战，另做准备中取消和局域网直连负控。

19.20.27 在自动开房控制连接写出 prepare 时通知大厅已开始建票。收到对应房间的 prepared 后才发送正式票据。若交接 offer 未收到任何回复，同一选择和租约仍有效时，在 200 ms 后允许一次重试。取消、部分回复、无效回复或已写出 commit 均终止重试。

自动模式的 v1 租约发布 `autoPool:true`、`mode:"pve"`、`offline:true`、`runId`、`expiresAt` 和 `controlIpc:{v,port,nonce}`。三张原生难度卡一直可选。一次已登录的 Start 使用一条独立的回环 TCP 控制连接。每帧仍是 LF 结尾、最多 1024 字节的 JSON。

| 方向 | op | 全部字段 |
| --- | --- | --- |
| Node → Rust | `prepare` | `v:1`、`op`、`runId`、`nonce`、`requestId`、`mode`、`clientPort`、`rpcPort` |
| Rust → Node | `prepared` | `op`、`requestId` |

每次 Start 生成新的 UUID v4 作为 `requestId`。`mode` 使用 `Rush_PVE_Easy`、`Rush_PVE_Normal` 或 `Rush_PVE_Hard`。Rust 先将就绪房间和 `choiceId:requestId` 写入同代租约，再回复 prepared。Node 复验后发票。取消、返回大厅、重选、重新绑定和断线均关闭该选择的控制连接。随后对已发票据的 Query 走上面的 join 握手。

2026-11-22 已按 Rust `auto_pool.rs` 的 Prepare、publish 和 tick 核对协议。运行 `node --test integrations/native-menu/*.test.cjs`，62 项通过。local-archive、ProjectRebound V0.8.4 和 main 的托管服务分别经一次正常大厅登录完成香港选区、三档开房准备、发票与 Query，受控 TCP 对端读取到对应的 commit。真实游戏进局仍待同场玩家加入记录与客户端画面验证。

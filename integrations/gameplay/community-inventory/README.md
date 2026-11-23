# 本机 PvE 房主首次选角的军械库装备

社区服的 `LateJoinManager::OnPostLogin` 收到已登录玩家的游戏控制器。本改动在该方法入口读取本机房主的军械库，并使用 `APBPlayerController::ServerPreOrderInventory` 提交 6 名常规角色的初始选择。每个角色分别提交主武器、副武器、左挂载、右挂载、近战和机动设备。服务器沿用游戏原有的角色准入、配额和选角处理。[v0.8.4 原始登录方法](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/ServerLogic/LateJoinManager.cpp#L49)

工具箱根据已登记的本机房间、房间凭据和玩家登录身份返回一次选角快照。读取快照不会占用玩家之后的出生连接。游戏端在读取前后检查玩家是否已经选角，并先备齐所有角色的装备，再向这个玩家的控制器提交。成功返回后输出 `[BRT-INVENTORY] owner-preorder-submitted roles=6 slots=36`。这行日志记录方法调用已经返回，服务器是否采纳取决于游戏自己的处理。[登录业务](OwnerLogin.cpp) · [回包校验](LoginRows.hpp) · [工具箱房间服务](../../loadout/room-spawn.cjs)

| 维护者工作树 | 适用文件 |
| --- | --- |
| ProjectRebound v0.8.4，提交 `b3d7f83f3789111d4a460b9662a8dc4e06e6a934` | [v084-community.patch](v084-community.patch) |
| ProjectRebound main，提交 `74c68ac3e6007c36248c442a281c2b7b831cc53a` | [main-community.patch](main-community.patch) |
| BoundaryToolbox，在已有的本机新房主修复上增加登录快照。当前工作树已合入 | [toolbox-login-menu.patch](toolbox-login-menu.patch) |

社区源码补丁已经包含登录业务文件、原始登录方法的调用及对应的工程编译项。维护者在相应源码版本上使用 `git apply --check <patch>` 核对，再使用 `git apply <patch>` 应用所选版本。工具箱服务和社区登录业务各自编译、发布。仅更新工具箱时，正在使用的社区二进制仍执行其原有登录流程。

两条社区分支都用 MSVC x64 编译了实际登录方法、装备预订和 HTTP 请求三个翻译单元。使用同一份 C++ HTTP 客户端和正式工具箱服务，两个独立测试房主各自的 36 个槽均与原版档案接口逐槽一致。当前验证止于源码编译和本机服务，零售实局的选角显示、两把枪的配件与涂装、重生后恢复都还需要游戏端证据。原版预订接口能接收装备 ID。枪的各部件需要交给真实武器实例上的 `APBWeapon::InitWeapon`。本机验证与具体边界

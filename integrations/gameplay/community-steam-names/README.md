# 对局内的 Steam 显示名

本模块使用 Steam 版《边境》Build 14530908 自带并已初始化的 Steamworks 1.57。客户端从 [ISteamUser::GetSteamID](https://partner.steamgames.com/doc/api/ISteamUser#GetSteamID) 读取当前登录账户，只在它与本机控制器的玩家状态、连接和当前对局成员对应时，才从 [ISteamFriends::GetPersonaName](https://partner.steamgames.com/doc/api/ISteamFriends#GetPersonaName) 读取本人显示名。随后调用游戏原有的 `APlayerController::SetName` 提交本人的名字。名字仅作展示，不参与玩家 ID、房间准入、配装或认证。

原版比分行有独立的名字和短 ID。名字格 `UMG_PlayerState_Tip_Player.Text_Name` 的输入是 `PlayerStateUI.PlayerName`，原生 Blueprint 会把文字转成大写；短 ID 格 `UMG_PlayerState_Tip_ID` 则读取 `GetDefaultIDStr`。现在比分板打开时，仅将本视口中已绑定的真人姓名格与 Steam 返回值核对，保留原始大小写和中文。对于远端行，只请求这一行对应账户的 [Steam 昵称](https://partner.steamgames.com/doc/api/ISteamFriends#RequestUserInformation)，等待资料就绪之后再显示；不会给别人的控制器提交改名，也不把本人名字填到其他行。

Steam 未就绪、本人账户不符、远端名字仍未取得、实际玩家状态不匹配、名称不能完整解码时，都保留原生显示。原版主播模式（`CustomPlayerName`）开启时，不查询或覆盖真实名字。模块不会另行启动 Steam、抓取社区主页、保存昵称/ID 或输出它们的哈希。旧的专服网页抓取和按内存偏移写名代码已从这个构建中移除。

在 Windows 上使用 [受控回归](tests/verify.ps1) 检查 UTF-8、中文与补充字符、真实大小写、账户错配、等待远端资料、主播设置以及原生名字写回的重试界限。该回归只使用合成账户和本地替身，不读取测试机器的 Steam 身份。真实跨账号名字仍需两位实际玩家在同一局打开比分板验证。

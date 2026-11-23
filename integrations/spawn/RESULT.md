# Project Boundary 出生配装的 C++ 消费端

此交付面向能编译专服玩法模块的维护者。Windows C++ 消费端已连接临时启动的真实档案应用，从当次选择准备 DMR、FOP、左右挂载、近战和移动装置，处理重生与同命重试。样例中，第一名玩家获得金色 DMR、红色 FOP、宇宙麻雀、探针和钩索。第二名玩家拿到自己的涂装与蜂巢。零售 Build 14530908 的出生库存调用位置及整枪外观加载器还未接入，玩家实机画面的已知问题仍然存在。

出生读取和初始选角分开。选角在房主第一次调用 PlayerState 的 ClientInitFieldMod 时使用 OwnerRoleMenu.hpp。它只替换允许角色的军械库槽位，保留房间配额，同一连接部署后档案服务关闭此读取。

[SpawnConsumer.hpp](SpawnConsumer.hpp) 解析协议并约束房间、已认证身份、连接、角色、两把枪、生命序号与当前 Pawn。[BoundaryInventory.hpp](BoundaryInventory.hpp) 构造待发布的角色和枪械数据。[RoomHttp.hpp](RoomHttp.hpp) 与 [RoomHttp.cpp](RoomHttp.cpp) 通过 Windows WinHTTP 在房间配置的本机端口调用档案应用。依赖 C++17 与发行源码已有的 nlohmann JSON 单头文件。选择启用异常的 C++ 编译单元，WinHTTP 链接由实现文件声明。

## 宿主接入位置

1. 在专服完成玩家身份验证后，从认证结果取得 SteamID64，给此连接分配 UUID，构造 `Member`、`Consumer` 并调用 `RoomHttp::Admit`。每个连接持有自己的消费对象。房间凭据由专服配置提供。
2. 在服务器批准玩家本次角色及武器、准备创建 Pawn 库存时，调用 `Consumer::Begin(life, pawnGeneration, selection)`。首生从 1 开始，每次实际重生递增。宿主用自己的 Pawn 代次或标识将异步回包与当前实例对应。在后台线程调用 `RoomHttp::Prepare`。
3. 回游戏线程后，从仍在场的连接取得当前 Pawn，调用 `AcceptAtInventorySpawn`，传入这次角色与枪型的基础 `FPBRoleNetworkConfig`、宿主的普通 FName 构造器及建立未发布库存的方法。以宿主已有的类选择和基础配置为准。函数保留角色外观与已批准武器类，按 1 至 10 号槽写入部件、部件皮肤、特殊准星、部件涂装、枪饰。六个角色槽与左右挂载、近战、移动字段一起传给正常库存创建流程。明确卸空的枪和挂载进入 `None`，前一命的槽位不继承到下一命。
4. `BeginBatch` 是接入方提供的普通库存创建函数。它按传入角色配置为本玩家建立尚未公开的枪械与挂载，返回的批次提供 `PrimaryWeapon`、`SecondaryWeapon`、`StageAppearance` 和 `Publish`。这四个函数是本组件对宿主的要求。宿主通过自己的普通外观数据表和材质接口把 `StageAppearance` 收到的整枪皮肤、涂装和额外挂饰应用到相应实例，保留未发布状态。填装函数随后对存在的两把枪各调用一次 `APBWeapon::InitWeapon(config, false)`。最后由 `Publish` 一次完成原生装备发布和同步。解析或外观准备失败时，批次由原创建流程回收，保留同命重试机会。同版 InitWeapon 声明 · 同版武器配置 · 同版角色配置
5. 离房时在游戏线程先调用 `Consumer::Leave`，随后在后台调用 `RoomHttp::Leave`。异步持有宿主的弱连接引用，回包投递时重新检查有效性。

## 已运行验证

- MSVC 用 C++17 构建并执行消费端，从业务服务产生的四份响应逐项比较了两把枪的全部 20 个部件及其外观、整枪涂装、枪饰、准星、角色挂载与基础类型。保留了同枪基础数据。第二命卸空副枪及右挂载。重新提交同一命只发布一次。错房、错连接、错人、错角色、旧生命、错误 Pawn、重复槽位、未批准枪类、外观准备失败、准备期间离房等分支都没有发布新装备。[回归与输入](verify.cpp) · [录制的测试数据](cases/primaryLife.json)
- 本机应用测试用随机端口、独立房间凭据和临时玩家档案。C++ WinHTTP 依次完成登记、首生准备、同命重试、重生准备和离房，并用类型化的测试枪验证装备被提交。[测试入口](verify-http.cjs) · [初始选角与出生对照](verify.cpp)
- MSVC 以 `/Zs` 对同版本公开声明检查了 `APBWeapon::InitWeapon` 的成员函数签名、角色与武器配置、槽位枚举及模板构造。游戏运行时类的材质加载和实际 Pawn 发布仍需维护者提供上述两个宿主入口。项目现有公开源码里未找到拥有此出生路径的游戏模块、可链接的游戏实现或可从零售端正常加载的内容插件。[声明检查](declaration-check.cpp)

复核脚本从自身目录读取随附的四份合成玩家响应。`BRT_SDK_ROOT` 或参数 `-Community` 可指定社区工程根目录或其 `ProjectReboundMainDLL` 目录。`BRT_SPAWN_CASES` 可替换样例目录。实际 HTTP 联测通过 `BRT_SPAWN_FIXTURE_KIT` 选择提供 `fixture.cjs`、`fixture-kit.cjs` 和 `probe-minifarm-sample.json` 的测试目录，通过 `BRT_ARCHIVE_SOURCE` 或位置参数选择档案服务源码。下列命令也可在移动后的源码包中运行。

```powershell
.\verify.ps1
node .\verify-http.cjs 'C:\BrtWork\SteamLibrary\steamapps\common\Boundary\ProjectBoundary\Binaries\Win64\BoundaryMetaServer-main'
```

房主可在其 BRT 档案目录的 `spawn-rooms.local.json` 注册本机专服。JSON 对象的每个键是房间名，值为该房间独立生成的 32 字节随机密钥的小写十六进制编码。将同一个房间凭据和本机 HTTP 端口配置给受信任的专服。工具箱仅向受管档案进程传入配置。初始选角入口与出生入口都使用该房间中同一条由专服认证的连接。

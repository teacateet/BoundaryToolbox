# Boundary 本机档案运行时

正常点击工具箱的 PvE / PvP 启动入口时，登录服务先核验所选安装，匹配后自动启动托管副本，无需单独运行部署脚本。游戏仍使用 Node HTTP 建立会话，由原生档案 RPC 读写。此模块不安装游戏、不改原服务目录，也不搜索其他安装的档案。[启动分支](../../src/core/runtime_ops.rs) · [发布与进程归属](../../src/core/local_profile.rs)

## 支持来源

| 所选 `BoundaryMetaServer-main` | 识别依据 | 实际使用的档案实现 |
| --- | --- | --- |
| ProjectRebound V0.8.4 的 `Release.zip` | 入口 `14ae33e2`、存储 `69864e5d`、41 份发布版协议、定义和依赖树 | 从所选安装读取发布版源码，在托管副本中补齐军械回包、列表和角色键处理 |
| 独立 BoundaryMetaServer `main` 提交 `007850d0` | 入口 `de98c06c`，Windows 检出 CRLF 版 `f605688f`；23 份协议、13 份定义；此版没有 `loadoutStore.js` 或 `definitionIndex.js` | 从工具箱内的 V0.8.4 源码包提供持久化服务，用所选安装的游戏定义和已验证 Node 依赖。上游 main 自身的角色/配件更新仅回执，读取返回静态默认值 |
| 本机已修订源码 | 入口 `3961c933`、存储 `c85d1cc7`；兼容既有文档列出的已安装组合 | 保持既有档案及投影，同样使用 REST / RPC 的角色键规则 |

安装器会先解开 ProjectRebound，再安装独立 MetaServer，因此发布包和最终安装目录的入口可能不同。即使安装记录不含备份，已识别的 `main` 也可独立启动。没有把未来的 `main`、旧标签、任意自定义分支或混合协议列作兼容版本。未识别的版本返回 `Unsupported` 及原因，交回启动器既有处理，不启用本模块能力。[V0.8.4 发布](https://github.com/STanJK/ProjectRebound/releases/tag/V0.8.4) · [独立 MetaServer 固定提交](https://github.com/STanJK/BoundaryMetaServer/tree/007850d0fd07c688d0a1ec1ff2a8771726c3463c)

## 档案目录

三种来源均只使用所选 `<Win64>/BoundaryMetaServer-main/data/loadouts/<playerId>.json`。此路径已经是本地目录连接时，复用其实际目标；首次安装仅在首次写入时创建。独立 main 本无原生持久化目录，此处采用发布版存储约定。工具箱不读取或导入发布包自带的示例档案、不扫描备份、不跨安装合并，不把托管应用目录当存档。两个已存在的大小写精确键仍独立；唯一别名才复用原键。

`<installer_home>/profile-runtime/apps/<SHA-256>/profile-location.json` 记录 `source`、`sourceKind` 和 `loadoutDir`。服务仅监听回环，本机匹配服务固定在回环，无云同步。启动器检查实际档案路径和工具箱目录的 Windows 卷类型，拒绝 UNC 和映射网络卷。不要将档案放入另行配置的云同步目录。[Windows 卷类型](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdrivetypew)

## 本机人机匹配

三种来源均由生产 KIT 应用 `session-progress.patch` 后再应用 `ready-room.patch`，并部署两个同名 `.cjs`。只进大厅或查列表不会分配；未启用房间时保持原回环匹配器和同一档案目录。升级发布新的内容摘要目录，不改写旧应用或玩家 JSON。

房间管理器只需调用 `local_profile::running_pve_room_lease(installer_home, &launch_files(target)) -> Result<Option<PathBuf>>`。它只读核验此安装正在监听的本机档案进程、当前 KIT 的 receipt、应用和档案路径；返回 `Some(<home>/profile-runtime/pve-ready-<实际运行应用摘要>.json)` 或 `None`，不创建记录、不猜测最近目录。拒绝其他目标、旧 KIT 的进程、菜单组件漂移和非普通记录文件。写入方应在刷新前复核归属，按[房间记录的字段、续期和撤销约定](../native-menu/RESULT.md)原子替换。API 只确认档案服务，游戏服的进程、地图、模式及端口仍由写入方确认。

## 复验

在项目根目录运行 `node integrations/profile-runtime/verify.cjs`。可用 `PROFILE_TEST_RELEASE` 指定原版 ZIP，`PROFILE_TEST_MAIN` 和 `PROFILE_TEST_MAIN_MODULES` 指定独立 main 的源码与依赖。原本机基线仍由 `PROFILE_TEST_SOURCE`、`PROFILE_TEST_MODULES` 提供。测试使用合成账号、随机回环端口和独立探针，不启动游戏。对待发布 EXE 执行 `node integrations/profile-runtime/verify-menu.cjs --embedded <BRT.exe>` 可只读核对菜单组件。[结果及完整指纹](RESULT.md)

`pack-release.cjs <Release.zip>` 供维护时重建源码包，只接受固定 SHA-256 的官方资产。压缩内容是可解压查看的 UTF-8 JSON，键为源码路径，值为原始文本。清单仅含 3 份 JS 和 41 份 protobuf；许可证与源码包一并嵌入。安装时不执行生成器，也不下载源码。[源文件生成规则](pack-release.cjs) · [发布版到已测基线的差异](release-084.patch) · [原始许可证](release-084-LICENSE.txt)

# 社区专服和客户端业务组合验收

本轮组合新增[开场动画后的首次出生](../first-life/README.md)。两个源码版本各应用 12 份补丁，编译 11 个业务及界面文件。逐版本回执包含编译来源和对应静态库的 SHA-256。[本轮编译结果](verification.json)

验收覆盖首次选角、出生武器、专服钩索状态、重连、挂载装填、麻雀瞄环、头顶标记同步至小地图、死亡选角、27 款挂载的本地视口，以及钩索二次使用的客户端观察。每版都在干净源码上应用这些改动，并用 MSVC x64 将最终的 11 个业务和 UMG 源文件编入同一份静态库。[逐版本回执](verification.json)

| 源码版本 | 服务端入口 | 客户端入口 |
| --- | --- | --- |
| V0.8.4，提交 `b3d7f83` | `LateJoinManager.cpp` 连接初次登录、出生及待完成的重连，`ServerLogic.cpp` 读取原生钩索状态。 | `UIFix.cpp` 同时包含瞄环、雷达、挂载视口、钩索输入观察和死亡选角。切图由 `ClientLogic.cpp` 确认，挂载装填由 `LauncherFix.cpp` 处理。 |
| 拆分主线，提交 `74c68ac` | `LateJoin.cpp` 连接初次登录、出生及待完成的重连，`RoundManager.cpp` 读取原生钩索状态。 | `UIShake.cpp` 同时包含瞄环、雷达、挂载视口和钩索输入观察。死亡选角和切图由 `AutoConnect.cpp` 处理，挂载装填由 `SideMountFixClient.cpp` 处理。 |

每版查验了 9 对调用方和实现方的完整 C++ 符号，涵盖登录、出生、重连，以及小地图画布、点迹更新和原版弹窗的返回动作。每版也做了 4 个反例。提前应用重连或雷达、重复应用首次选角，Git 都拒绝了。将弹窗实现从静态库中拿掉后，档案成员数从 11 降为 10，符号表随之失去弹窗返回动作。本次验收范围到普通业务静态库，Steam 局内表现仍需现场验证。

在本目录执行以下命令可复算。输入 JSON 的 `checkout` 取拆分主线仓库根目录，`releaseRoot` 取干净发行源码的 `Payload` 目录，`releaseSdk` 取含发行版 `SDK` 子目录的 `Payload` 目录。已分开存放的发行源文件可通过 `releaseFiles` 按模块内相对路径指定。验收逐项对照固定的上游 Git blob，原始文件变化时会退出。输出写入独立的临时目录。

```powershell
node ./prepare.cjs ./inputs.json ./build
$run = (Get-Content -Raw ./build/current-run.txt).Trim()
./compile.ps1 -Run $run -Version v084
./compile.ps1 -Run $run -Version main
node ./verify.cjs $run ./build/verification.json
```

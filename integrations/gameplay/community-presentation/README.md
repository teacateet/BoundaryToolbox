# 社区客户端挂载装填显示

原社区客户端每次收到 `Reloading` 通知，都会调用一次开始装填，再补一次单发装入。第二份相同通知会重播动画，并可能让刚刚收起的挂载又显示弹体。这里的改动位于现有 `HandleLauncherClientEvent` 中的装填分支，适用所有继承 `APBLauncher` 的挂载。[V0.8.4 原始实现](https://github.com/STanJK/ProjectRebound/blob/b3d7f83f3789111d4a460b9662a8dc4e06e6a934/Payload/Utility/LauncherFix.cpp#L109)

进入新的装填状态时仍播放原装填事件。补播单发装入前，代码确认挂载归属一名活着的角色，并且仍在这个人的左槽或右槽。手持控制器的挂载还需处于当前选择。对于本地视图，角色的手必须正在展开或已经展开控制器。可以与枪同时工作的辅助挂载可以凭当前选择或启用状态继续装填。游戏自行发出的逐发装入事件继续正常分派。

| 社区源码布局 | 对应差异 |
| --- | --- |
| `Payload/Utility/LauncherFix.cpp`，发行标签 V0.8.4 | [v084.patch](v084.patch) |
| `ProjectReboundMainDLL/Client/SideMountFixClient.cpp`，已拆分客户端文件的检出 | [split.patch](split.patch) |

在 Windows 的 Visual Studio 2022 C++ Build Tools 环境中，以下命令把差异应用到独立副本，检查可回退性，用其 SDK 编译原版和修订版的完整源文件，再用同一 21 项动作测试两份完整客户端回调。测试生成的 `callback-test.exe`、逐项结果和 `results.json` 保存在 `-OutDir` 下新建的目录。提供匹配的社区检出路径即可，拆分版使用 `-Layout split`。[验证脚本](verify.ps1) · [动作与断言](callback.test.cpp)

```powershell
.\verify.ps1 -CommunityRoot 'D:\src\ProjectRebound' -OutDir "$env:TEMP\BrtPodRegression" -Layout v084
```

此回归能核对社区额外调用了几次 Blueprint。录像中的六格麻雀瞄环与主动切枪时探针模型的最终显示，需要零售游戏内的相同动作验证。

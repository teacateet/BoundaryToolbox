# 宇宙麻雀六格瞄环

这份社区源代码修正在切枪、收起控制器、死亡和失去角色控制权时收起原作的六格瞄环。首次出生时，即使挂载拥有者尚未同步，只要它已在本地角色的左肩或右肩、与瞄环双向关联，并且控件明确属于当前视口，也会先收起瞄环。待归属确认且角色实际部署该挂载后，才恢复原透明度；若归属确认前控件失去该视口，则解除调整。不会修改其他玩家的瞄环。

发行标签 V0.8.4 使用 `v084.patch` 修改 `Payload/Utility/UIFix.cpp`，拆分检出使用 `split.patch` 修改 `ProjectReboundMainDLL/Client/UIShake.cpp`。将同目录的 `SparrowReticle.hpp` 放到该源文件旁边。两份差异都通过原有的角色界面回调调用此逻辑。[业务实现](SparrowReticle.hpp)

以下脚本会在独立副本中应用差异，检查回退，用当前检出的 SDK 分别编译原版、修订版的完整 UI 源文件，再验证两者的 37 项控件动作（含首次出生和死亡时归属延迟、其他视口及其他角色）。这是社区源码的回调测试；不能单独证明 Steam 零售对局已加载修订。拆分版用 `-Layout split`。[复核脚本](verify.ps1) · [动作与断言](reticle.test.cpp)

```powershell
.\verify.ps1 -CommunityRoot 'D:\src\ProjectRebound' -OutDir "$env:TEMP\BrtReticle" -Layout v084
```

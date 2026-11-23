# 挂载挡住本地视野

社区客户端在挂载收起、玩家死亡或失去控制时，从该玩家的相机画面中收回挂载的第一人称模型及其自有子件。重新举起时移除自己添加的排除项，让原作继续决定哪些模型可见。宇宙麻雀的三枚第一人称导弹独立于三枚第三人称导弹。探针投放后有自己的 Actor 和生命周期。[Epic 对玩家相机组件排除列表的说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/APlayerController?application_version=5.5)

两份补丁都接入社区原有的角色界面事件。v0.8.4 使用 `v084.patch`，将 `PodView.hpp` 放入 `Payload/Utility`。拆分版使用 `split.patch`，将同一头文件放入 `ProjectReboundMainDLL/Client`。补丁可以叠加在雷达、麻雀准星和死亡菜单之后。

首次出生时，肩位和挂载拥有者可能分两次同步。如果本地相机已经控制该角色，肩位已经指向挂载，而挂载还没有报告拥有者，就先从这台相机排除挂载自身的第一人称模型。切换肩位、换视角或收到另一角色的归属时立即撤回临时处理；只有归属和部署状态都确认后才显示。已投出的探针、第三人称部件和其他相机不受影响。

验证脚本从原作挂载数据表读取全部 27 行，追踪 Blueprint 父类和原生 SDK 继承关系，并逐项测试左右肩。15 款使用手持控制器，12 款按独立的发射或激活状态显示。132 项对照覆盖常规操作、先收到肩位后收到拥有者、跨死亡和切枪。程序运行原版与候选的实际界面入口，并编译对应的完整社区源码。零售游戏中的最终画面及挂载按键仍需新一局验证。

```powershell
.\verify.ps1 -Layout v084 -CommunityRoot D:\path\to\v084-source -OutDir D:\path\to\verification
.\verify.ps1 -Layout split -CommunityRoot D:\path\to\rebound-source -OutDir D:\path\to\verification
```

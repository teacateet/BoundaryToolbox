#pragma once
#include "BrtGrappleCycle.h"

// 这些续调包住普通游戏模块原有的一段命中校验、物理、特效或者通知。
// 原玩法异步工作按值保存 Use，同一次生命里也可能收到上一发的回执。
namespace BrtGrappleGame
{
    template <class TModule, class TContinue>
    bool Locked(TModule& Module, FBrtGrappleUse Use, TContinue StartOriginalPull)
    {
        if (!Module.BrtNativeLocked(Use)) return false;
        StartOriginalPull();
        return true;
    }

    template <class TModule, class TContinue>
    bool Recycle(TModule& Module, FBrtGrappleUse Use, TContinue StartOriginalReturn)
    {
        if (!Module.BrtNativeRecycling(Use)) return false;
        StartOriginalReturn();
        return true;
    }

    template <class TModule, class TContinue>
    bool Returned(TModule& Module, FBrtGrappleUse Use, TContinue NotifyOriginalCooldownStart)
    {
        if (!Module.BrtAcknowledgeReturned(Use)) return false;
        NotifyOriginalCooldownStart();
        return true;
    }
}

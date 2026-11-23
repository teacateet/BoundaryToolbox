#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

// 服务器签发当前装备生命期的使用序号。旧生命、旧装备和上一发的回执不会作用于新一发。
struct FBrtGrappleUse
{
    std::uint32_t Life = 0;
    std::uint32_t Use = 0;
    bool Valid() const { return Life != 0 && Use != 0; }
    bool operator==(FBrtGrappleUse Other) const { return Life == Other.Life && Use == Other.Use; }
};

enum class EBrtGrapplePhase { Unbound, Ready, Preparing, AwaitingNative, Flying, Locked, Recycling, Cooling };
enum class EBrtGrappleReturn { NativeCallback, SampledReady };

class FBrtGrappleAuthority
{
    FBrtGrappleUse Ticket;
    EBrtGrapplePhase Phase = EBrtGrapplePhase::Unbound;
    bool ReleaseSeen = false;
    double EndsAt = 0.0;

    bool At(FBrtGrappleUse Key, EBrtGrapplePhase Expected) const
    {
        return Key.Valid() && Key == Ticket && Phase == Expected;
    }

    bool NextUse()
    {
        if (Ticket.Use == std::numeric_limits<std::uint32_t>::max()) { Unbind(); return false; }
        ++Ticket.Use;
        Phase = EBrtGrapplePhase::Ready;
        EndsAt = 0.0;
        ReleaseSeen = false;
        return true;
    }

public:
    FBrtGrappleUse BindNewLife()
    {
        Unbind();
        if (Ticket.Life == std::numeric_limits<std::uint32_t>::max()) return {};
        ++Ticket.Life;
        Ticket.Use = 1;
        Phase = EBrtGrapplePhase::Ready;
        return Ticket;
    }

    void Unbind()
    {
        Ticket.Use = 0;
        Phase = EBrtGrapplePhase::Unbound;
        EndsAt = 0.0;
        ReleaseSeen = false;
    }
    FBrtGrappleUse Current() const { return Ticket; }
    EBrtGrapplePhase State() const { return Phase; }
    double Deadline() const { return EndsAt; }
    bool Press(FBrtGrappleUse Key)
    {
        if (!At(Key, EBrtGrapplePhase::Ready)) return false;
        Phase = EBrtGrapplePhase::Preparing;
        return true;
    }
    bool Release(FBrtGrappleUse Key)
    {
        if (!Key.Valid() || !(Key == Ticket) || ReleaseSeen) return false;
        if (Phase != EBrtGrapplePhase::Preparing && Phase != EBrtGrapplePhase::AwaitingNative && Phase != EBrtGrapplePhase::Flying
            && Phase != EBrtGrapplePhase::Locked) return false;
        ReleaseSeen = true;
        return true;
    }
    bool AwaitingLaunch(FBrtGrappleUse Key) const { return At(Key, EBrtGrapplePhase::Preparing); }
    bool ClaimValidatedLaunch(FBrtGrappleUse Key)
    {
        if (!AwaitingLaunch(Key)) return false;
        Phase = EBrtGrapplePhase::AwaitingNative;
        return true;
    }
    bool CancelOrFail(FBrtGrappleUse Key)
    {
        return (At(Key, EBrtGrapplePhase::Preparing) || At(Key, EBrtGrapplePhase::AwaitingNative)) && NextUse();
    }
    bool NativeLaunch(FBrtGrappleUse Key)
    {
        if (!At(Key, EBrtGrapplePhase::AwaitingNative)) return false;
        Phase = EBrtGrapplePhase::Flying;
        return true;
    }
    bool NativeLock(FBrtGrappleUse Key)
    {
        if (!At(Key, EBrtGrapplePhase::AwaitingNative) && !At(Key, EBrtGrapplePhase::Flying)) return false;
        Phase = EBrtGrapplePhase::Locked;
        return true;
    }
    bool NativeRecycle(FBrtGrappleUse Key)
    {
        if (!At(Key, EBrtGrapplePhase::AwaitingNative) && !At(Key, EBrtGrapplePhase::Flying)
            && !At(Key, EBrtGrapplePhase::Locked)) return false;
        Phase = EBrtGrapplePhase::Recycling;
        return true;
    }
    bool NativeReturned(FBrtGrappleUse Key, double ServerNow, double Duration, EBrtGrappleReturn Evidence)
    {
        // 待命也是发射前的状态。若没看到这发活动过，只认按本发序号交付的原生收回回调。
        const bool returned = At(Key, EBrtGrapplePhase::Flying) || At(Key, EBrtGrapplePhase::Locked)
            || At(Key, EBrtGrapplePhase::Recycling)
            || (Evidence == EBrtGrappleReturn::NativeCallback && At(Key, EBrtGrapplePhase::AwaitingNative));
        if (!returned || !std::isfinite(ServerNow)
            || !std::isfinite(Duration) || Duration <= 0.0 || !std::isfinite(ServerNow + Duration)) return false;
        EndsAt = ServerNow + Duration;
        Phase = EBrtGrapplePhase::Cooling;
        return true;
    }
    bool TimerExpired(FBrtGrappleUse Key, double ServerNow)
    {
        return At(Key, EBrtGrapplePhase::Cooling) && std::isfinite(ServerNow) && ServerNow >= EndsAt && NextUse();
    }
};

// 本机只在新按下沿消费服务器给的下一次使用序号。长按、松开、回收动画和 HUD 都不会解锁。
class FBrtGrappleOwnerInput
{
    FBrtGrappleUse Available;
    FBrtGrappleUse PressedUse;
    FBrtGrappleUse ActiveUse;
    std::uint32_t LastLife = 0;
    std::uint32_t LastUse = 0;
    bool Down = false;
    bool Retired = false;
public:
    bool ObserveReady(FBrtGrappleUse Key)
    {
        if (!Key.Valid() || Key.Life < LastLife || (Key.Life == LastLife && (Retired || Key.Use <= LastUse))) return false;
        if (Key.Life != LastLife) { PressedUse = {}; Down = false; Retired = false; }
        LastLife = Key.Life;
        LastUse = Key.Use;
        ActiveUse = {};
        Available = Key;
        return true;
    }
    bool Retire(std::uint32_t Life)
    {
        if (Life < LastLife || (Life == LastLife && Retired)) return false;
        LastLife = Life;
        LastUse = 0;
        Retired = true;
        Available = {}; PressedUse = {}; ActiveUse = {}; Down = false;
        return true;
    }
    FBrtGrappleUse Press()
    {
        if (Down) return {};
        Down = true;
        PressedUse = Available;
        if (PressedUse.Valid()) ActiveUse = PressedUse;
        Available = {};
        return PressedUse;
    }
    FBrtGrappleUse Release()
    {
        if (!Down) return {};
        Down = false;
        const FBrtGrappleUse Key = PressedUse;
        PressedUse = {};
        return Key;
    }
    FBrtGrappleUse Action() const { return ActiveUse; }
};

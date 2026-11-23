// 拥有客户端、房主、正常武器碰撞与可控的游戏时钟。编译游戏源码的计时器和 RPC 实现。
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <tuple>
#include <vector>
#include "../Source/GrappleGameCallbacks.h"
using uint32 = std::uint32_t;
#define UFUNCTION(...)
template<class T> struct TWeakObjectPtr {
    T* Ptr=nullptr; T* Get() const {return Ptr;}
    TWeakObjectPtr& operator=(T* Value) {Ptr=Value;return *this;}
    void Reset() {Ptr=nullptr;}
};
template<class T> bool IsValid(T* Value) {return Value!=nullptr;}
struct FTimerHandle { int Slot=0; };
struct FTimerDelegate {
    std::function<void()> Callback;
    template<class T, class K> static FTimerDelegate CreateUObject(T* Obj, void(T::*Method)(K), K Key) {
        return { [=] { (Obj->*Method)(Key); } };
    }
};
struct UWorld {
    float Now=0.0f;
    int Last=0;
    std::map<int,std::pair<float,FTimerDelegate>> Pending;
    UWorld& GetTimerManager() {return *this;}
    float GetTimeSeconds() const {return Now;}
    void ClearTimer(FTimerHandle& Handle) {Pending.erase(Handle.Slot);Handle.Slot=0;}
    void SetTimer(FTimerHandle& Handle, FTimerDelegate Fn, float Delay, bool) {
        ClearTimer(Handle);Handle.Slot=++Last;Pending[Handle.Slot]={Now+Delay,Fn};
    }
    void At(float When) {
        Now=When;
        for (auto it=Pending.begin();it!=Pending.end();) {
            if (it->second.first>When) {++it;continue;}
            auto f=it->second.second;it=Pending.erase(it);f.Callback();
            it=Pending.begin();
        }
    }
};
enum class EPBHookStatus {Ready,Launching,Locked,Recycling};
struct APBCatchingModule;
struct APBCharacter {
    bool Alive=true, Local=false;
    APBCatchingModule* Equipped=nullptr;
    bool IsAlive() const {return Alive;}
    bool IsLocallyControlled() const {return Local;}
    APBCatchingModule* GetCurrentMobilityModule() {return Equipped;}
};
struct FConfig { float HookCoolDown=8.0f, SnapForce=400000.0f, MaxSnappingSpeed=1500.0f, HookLaunchMaxDistance=2500.0f; };
struct FPBCaughtData {float X;bool Solid;};
struct APBCatchingModule {
    APBCharacter* OwnerCharacter=nullptr;
    APBCatchingModule* Peer=nullptr;
    UWorld* World=nullptr;
    FConfig DataConfig;
    EPBHookStatus CurrentHookStatus=EPBHookStatus::Ready;
    bool Server=false,bIsEnabled=true,bIsInCoolDown=false;
    int EndNotices=0, ReadyNotices=0;
    int NativeValidationCalls=0, AcceptedLaunches=0;
    std::function<void(const FPBCaughtData&)> OriginalLaunch;
    bool ServerLaunchHook_Validate(const FPBCaughtData& Hit) { ++NativeValidationCalls;return std::isfinite(Hit.X); }
    void ServerLaunchHook_Implementation(const FPBCaughtData& Hit) { OriginalLaunch(Hit); }
    bool HasAuthority() const {return Server;}
    UWorld* GetWorld() const {return World;}
    void ClientEndHookLockCoolDown() {Peer->bIsInCoolDown=false;++Peer->EndNotices;}
    bool BrtServerInputEdge_Validate(uint32,uint32,bool);
    void BrtServerInputEdge_Implementation(uint32,uint32,bool);
    void BrtClientAvailability_Implementation(uint32,uint32);
    bool BrtServerLaunchTagged_Validate(uint32,uint32,const FPBCaughtData&);
    void BrtServerLaunchTagged_Implementation(uint32,uint32,const FPBCaughtData&);
#include "../Source/GrappleModule.declarations.inc"
};
void APBCatchingModule::BrtServerInputEdge(uint32 Life,uint32 Use,bool Press) {
    if (Peer->BrtServerInputEdge_Validate(Life,Use,Press)) Peer->BrtServerInputEdge_Implementation(Life,Use,Press);
}
void APBCatchingModule::BrtClientAvailability(uint32 Life,uint32 Use) {
    if (Use) ++Peer->ReadyNotices;
    Peer->BrtClientAvailability_Implementation(Life,Use);
}
void APBCatchingModule::BrtServerLaunchTagged(uint32 Life,uint32 Use,const FPBCaughtData& Target) {
    if (Peer->BrtServerLaunchTagged_Validate(Life,Use,Target)) Peer->BrtServerLaunchTagged_Implementation(Life,Use,Target);
}
#include "../Source/GrappleModule.inl"

static void Check(bool ok,const char* label) {
    if (!ok) {std::fprintf(stderr,"FAILED %s\n",label);std::exit(1);}
    std::printf("ok %s\n",label);
}
using FTarget = FPBCaughtData;
struct FHost {
    UWorld Clock;
    APBCatchingModule Room, Owner;
    APBCharacter Local, Server;
    int Launches=0, Pulls=0, Returns=0, Starts=0;
    float X=0;
    std::vector<float> Positions;
    FHost() {
        Room.Peer=&Owner;Owner.Peer=&Room;Room.World=&Clock;Owner.World=&Clock;Room.Server=true;
        Local.Local=true;Local.Equipped=&Owner;Server.Equipped=&Room;Room.OwnerCharacter=&Server;Owner.OwnerCharacter=&Local;
        Room.OriginalLaunch=[&](const FTarget& Target){
            const auto captured=Room.BrtNativeLaunchUse();
            if (!Target.Solid || std::abs(Target.X-X)>Room.DataConfig.HookLaunchMaxDistance) {Room.BrtNativeLaunchFailed(captured);return;}
            Room.CurrentHookStatus=EPBHookStatus::Launching;
            if (Room.BrtNativeLaunched(captured)) ++Launches;
        };
        Room.BrtRefreshServerLife();
    }
    bool Fire(FBrtGrappleUse key,FTarget target) {
        const int count=Launches;
        if (key.Valid() && key==Owner.BrtOwnerRequest()) Owner.BrtOwnerLaunchTarget(target);
        else if (key.Valid() && Room.BrtServerLaunchTagged_Validate(key.Life,key.Use,target))
            Room.BrtServerLaunchTagged_Implementation(key.Life,key.Use,target);
        return Launches>count;
    }
    bool Lock(FBrtGrappleUse key,FTarget target) {
        Room.CurrentHookStatus=EPBHookStatus::Locked;
        Room.BrtObserveNativeLifecycle();
        return BrtGrappleGame::Locked(Room,key,[&] {
            // 此宿主的原生受力处理。待合回 UE 的模块只决定是否调用，不写推力。
            const float dt=0.2f, mass=80.0f;
            const float speed=std::min(Room.DataConfig.MaxSnappingSpeed,Room.DataConfig.SnapForce/mass*dt);
            X+=std::min(target.X-X,dt*speed);++Pulls;Positions.push_back(X);
        });
    }
    bool Recycle(FBrtGrappleUse key) {
        Room.CurrentHookStatus=EPBHookStatus::Recycling;
        Room.BrtObserveNativeLifecycle();
        return BrtGrappleGame::Recycle(Room,key,[&]{++Returns;});
    }
    bool Return(FBrtGrappleUse key) {
        Room.CurrentHookStatus=EPBHookStatus::Ready;
        Room.BrtObserveNativeLifecycle();
        return BrtGrappleGame::Returned(Room,key,[&]{++Starts;Owner.bIsInCoolDown=true;});
    }
};
// 原生回调可以在一次 Actor Tick 内完成多个阶段。用同一拥有者的第二次真实请求验收。
static void FrameGap(const char* mode) {
    FHost h;
    const bool directLock = !std::strcmp(mode,"direct-lock");
    const bool directRecycle = !std::strcmp(mode,"direct-recycle");
    const bool allInOne = !std::strcmp(mode,"explicit-return");
    const bool onlyReady = !std::strcmp(mode,"unconfirmed-ready");
    const bool observedFlight = !std::strcmp(mode,"flight-then-ready");
    const bool observedLock = !std::strcmp(mode,"lock-then-ready");
    Check(directLock || directRecycle || allInOne || onlyReady || observedFlight || observedLock,"recognized frame sequence");
    h.Room.OriginalLaunch=[&](const FTarget& target){
        Check(target.Solid,"original gameplay accepts solid target");
        ++h.Launches;
    };
    h.Owner.BrtOwnerPress();
    const auto first=h.Owner.BrtOwnerRequest();
    Check(h.Fire(first,{400,true}),"first request entered original launch");
    h.Owner.BrtOwnerRelease();
    h.Room.BrtObserveNativeLifecycle();
    Check(h.Clock.Pending.empty() && h.Owner.ReadyNotices==1,"initial Ready alone is not a completed return");
    if (onlyReady) {
        h.Clock.At(20.0f);
        h.Room.BrtObserveNativeLifecycle();
        Check(h.Clock.Pending.empty() && h.Owner.ReadyNotices==1,"waiting for original launch cannot manufacture a second use");
        return;
    }
    if (directLock || observedLock) {
        h.Room.CurrentHookStatus=EPBHookStatus::Locked;
        if (observedLock) h.Room.BrtObserveNativeLifecycle();
        else {
            auto pull=[&]{++h.Pulls;};
            Check(BrtGrappleGame::Locked(h.Room,first,pull) && !BrtGrappleGame::Locked(h.Room,first,pull) && h.Pulls==1,
                  "native lock before first Tick delivers original pull exactly once");
        }
    } else if (directRecycle) {
        h.Room.CurrentHookStatus=EPBHookStatus::Recycling;
        auto recycle=[&]{++h.Returns;};
        Check(BrtGrappleGame::Recycle(h.Room,first,recycle) && !BrtGrappleGame::Recycle(h.Room,first,recycle) && h.Returns==1,
              "native recycle before first Tick delivers original return exactly once");
    } else if (observedFlight) {
        h.Room.CurrentHookStatus=EPBHookStatus::Launching;
        h.Room.BrtObserveNativeLifecycle();
    }
    h.Clock.At(2.0f);
    h.Room.CurrentHookStatus=EPBHookStatus::Ready;
    if (!allInOne) {
        h.Room.BrtObserveNativeLifecycle();
        Check(h.Clock.Pending.size()==1,"observed activity followed by Ready arms cooldown without a return callback");
    }
    auto start=[&]{++h.Starts;};
    Check(BrtGrappleGame::Returned(h.Room,first,start) && !BrtGrappleGame::Returned(h.Room,first,start),
          "one actual return starts one cooldown despite missing intermediate Tick");
    h.Room.BrtObserveNativeLifecycle();
    Check(h.Clock.Pending.size()==1 && h.Starts==1 && h.Room.bIsInCoolDown,"no duplicate cooldown timer");
    h.Clock.At(9.999f);h.Owner.BrtOwnerPress();h.Owner.BrtOwnerRelease();
    Check(h.Owner.EndNotices==0 && h.Owner.ReadyNotices==1,"fresh press during cooldown cannot launch");
    h.Clock.At(10.0f);h.Owner.BrtOwnerPress();
    const auto second=h.Owner.BrtOwnerRequest();
    Check(second.Life==first.Life && second.Use==first.Use+1 && h.Fire(second,{600,true}) && h.Launches==2,
          "fresh press after eight seconds reaches original launch in the same life");
    Check(!BrtGrappleGame::Returned(h.Room,first,start) && h.Clock.Pending.empty(),"late old return cannot cool the second use");
}
int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"baseline")) {FrameGap(argv[1]);return 0;}
    FHost h;
    h.Owner.BrtOwnerPress();const auto first=h.Owner.BrtOwnerRequest();
    Check(first.Valid() && h.Fire(first,{400,true}),"first press reaches original valid launch");
    Check(!h.Fire(first,{400,true}),"duplicate validated launch cannot run twice");
    Check(h.Lock(first,{400,true}) && !h.Lock(first,{400,true}) && h.X>0,"first lock pulls once using original host physics");
    h.Owner.BrtOwnerRelease();
    Check(h.Recycle(first) && !h.Recycle(first),"first recycle starts once");
    h.Clock.At(2.0f);
    Check(h.Return(first) && !h.Return(first) && h.Starts==1 && h.Room.bIsInCoolDown,"one native return starts configured cooldown");
    auto staleTimer=h.Clock.Pending.begin()->second.second;
    h.Clock.At(9.999f);staleTimer.Callback();
    h.Owner.BrtOwnerPress();
    Check(!h.Fire(h.Owner.BrtOwnerRequest(),{600,true}) && h.Owner.EndNotices==0,"early timer and early press cannot advance cooldown");
    h.Clock.At(10.0f);
    Check(h.Owner.EndNotices==1 && !h.Room.bIsInCoolDown,"eight server seconds unlock once and notify original owner");
    h.Owner.BrtOwnerPress();
    Check(!h.Owner.BrtOwnerRequest().Valid(),"held key cannot fire automatically at expiry");
    h.Owner.BrtOwnerRelease();h.Owner.BrtOwnerPress();auto second=h.Owner.BrtOwnerRequest();
    Check(second.Life==first.Life && second.Use==first.Use+1 && h.Fire(second,{600,true}),"second fresh press launches in the same life");
    Check(h.Lock(second,{600,true}) && h.X>h.Positions.front() && h.Pulls==2,"second solid lock produces separate forward displacement");
    h.Owner.BrtOwnerRelease();h.Recycle(second);
    Check(!h.Room.BrtNativeReturned(first),"previous shot return cannot finish second shot");
    h.Clock.At(11.0f);h.Return(second);staleTimer.Callback();
    Check(h.Room.bIsInCoolDown && h.Owner.EndNotices==1,"old timer cannot shorten second cooldown");
    auto previousLifeTimer=h.Clock.Pending.begin()->second.second;
    h.Server.Alive=false;h.Room.BrtEndServerLife();
    Check(h.Clock.Pending.empty() && !h.Room.bIsInCoolDown,"death clears authoritative timer");
    h.Server.Alive=true;h.Room.BrtRefreshServerLife();
    h.Owner.BrtOwnerPress();auto third=h.Owner.BrtOwnerRequest();
    Check(third.Life>second.Life && !h.Fire(second,{700,true}),"previous-life request has no effect after respawn");
    previousLifeTimer.Callback();
    Check(h.Owner.EndNotices==1 && !h.Room.bIsInCoolDown,"late previous-life cooldown callback cannot act on respawn");
    Check(!h.Fire(third,{9999,true}) && h.Launches==2,"original target range validation survives");
    h.Owner.BrtOwnerRelease();h.Owner.BrtOwnerPress();auto fourth=h.Owner.BrtOwnerRequest();
    Check(!h.Fire(fourth,{600,false}) && h.Launches==2,"original solid target validation survives");
    h.Owner.BrtOwnerRelease();h.Owner.BrtOwnerPress();auto fifth=h.Owner.BrtOwnerRequest();
    h.Server.Equipped=nullptr;h.Room.BrtRefreshServerLife();
    Check(!h.Fire(fifth,{600,true}),"unequipping retires outstanding input");
    Check(h.Room.NativeValidationCalls>=7,"tagged owner RPC retains the game's original server validation");
    std::printf("sameLife=%u attempts=%u,%u launch=%d pull=%d positionsCm=0,%.0f,%.0f cooldownSeconds=%.0f\n",
        first.Life,first.Use,second.Use,h.Launches,h.Pulls,h.Positions[0],h.Positions[1],h.Room.DataConfig.HookCoolDown);
    if (argc==1) {
        for (const char* mode : {"unconfirmed-ready","direct-lock","direct-recycle","flight-then-ready","lock-then-ready","explicit-return"})
            FrameGap(mode);
    }
}

// 普通客户端回调的离线替身，只记录可观察到的 Blueprint 调用。
#include <cstdio>
#include <cstdint>
#include <string>

using uint8 = std::uint8_t;
enum class EPBLauncherState : uint8 { Standby, Deploying, Undeploying, Ready, Reloading, Handup };
enum class EPBHandsState { DeployedWeapon, DeployingLauncherController, DeployedLauncherController, UndeployingLauncherController };
enum class Kind { Pawn, Character, Launcher, Deploy, DeployBP };
struct UFunction {};
struct UObject {
    bool Valid = true;
    virtual bool IsA(Kind) const { return false; }
    std::string GetFullName() const { return "test"; }
};
struct UKismetSystemLibrary { static bool IsValid(const UObject* obj) { return obj && obj->Valid; } };
struct APawn : UObject {};
struct AController { bool Local = true; bool IsLocalPlayerController() const { return Local; } };
struct APBLauncher;
struct APBVehicle : UObject { APBLauncher* LeftLauncher{}; APBLauncher* RightLauncher{}; };
struct APBCharacter : APawn {
    APBVehicle* CurrentVehicle{};
    APBLauncher* CurrentLeftLauncher = nullptr;
    APBLauncher* CurrentRightLauncher = nullptr;
    APBLauncher* CurrentLauncher = nullptr;
    AController* Controller = nullptr;
    EPBHandsState CurrentHandsState = EPBHandsState::DeployedLauncherController;
    bool Living = true, FirstPerson = true;
    static Kind StaticClass() { return Kind::Character; }
    bool IsA(Kind kind) const override { return kind == Kind::Character; }
    bool IsAlive() const { return Living; }
    bool IsFirstPerson() const { return FirstPerson; }
    const AController* GetController() const { return Controller; }
};
struct APBLauncher : UObject {
    APawn* Pawn = nullptr;
    EPBLauncherState CurrentState = EPBLauncherState::Standby, PendingState = EPBLauncherState::Standby;
    struct { int AmmoInClip = 1, TotalAmmo = 9; } Magazine;
    int BurstCounter = 0, ReloadEvents = 0, RoundEvents = 0;
    bool bIsFiring = false, bPendingFiring = false, bIsFireControlEnabled = false, Active = false;
    APawn* GetOwnerPawn() const { return Pawn; }
    bool IsActivated() const { return Active; }
    bool HasInfiniteAmmo() const { return false; }
    bool HasAmmoInClip() const { return Magazine.AmmoInClip > 0; }
    void K2_Standby() {}
    void K2_Deploying() {}
    void K2_Undeploying() {}
    void K2_Ready() {}
    void K2_Handup() {}
    void K2_Reloading() { ++ReloadEvents; }
    void K2_ASingleAmmoReloaded() { ++RoundEvents; }
};
struct APBLauncher_Deploy : APBLauncher {
    static Kind StaticClass() { return Kind::Deploy; }
    bool IsA(Kind kind) const override { return kind == Kind::Deploy; }
};
struct APBLauncher_Deploy_BP_C : APBLauncher_Deploy {
    struct Tracer { void OnHidden_Event() {} };
    Tracer* ProjectilePathTracer = nullptr;
    static Kind StaticClass() { return Kind::DeployBP; }
    bool IsA(Kind kind) const override { return kind == Kind::DeployBP || APBLauncher_Deploy::IsA(kind); }
};
void ClientDebugLog(const std::string&) {}

// 验证脚本从用户提供的原版或已修改的社区源码逐字取出整个函数。
#include "callback.inc"

int main()
{
    int failures = 0;
    auto expect = [&](const char* name, bool matches) {
        std::printf("%s %s\n", matches ? "PASS" : "FAIL", name);
        if (!matches) ++failures;
    };
    auto notify = [](APBLauncher& pod, EPBLauncherState state) {
        pod.PendingState = state;
        return HandleLauncherClientEvent(&pod, nullptr, nullptr, "OnRep_PendingState");
    };
    const auto reload = EPBLauncherState::Reloading;
    const auto ready = EPBLauncherState::Ready;
    APBCharacter player, peer;
    AController local, remote;
    remote.Local = false;
    player.Controller = &local;
    peer.Controller = &remote;
    peer.FirstPerson = false;
    peer.CurrentHandsState = EPBHandsState::DeployedWeapon;
    APBLauncher_Deploy_BP_C left, right, otherPlayer;
    APBLauncher auxiliary;
    left.Pawn = right.Pawn = auxiliary.Pawn = &player;
    APBVehicle vehicle; player.CurrentVehicle = &vehicle;
    player.CurrentLauncher = vehicle.LeftLauncher = &left;
    vehicle.RightLauncher = &right;
    otherPlayer.Pawn = &peer;
    peer.CurrentLauncher = peer.CurrentRightLauncher = &otherPlayer;

    expect("normal callback continues", !notify(left, reload));
    expect("selected left first reload", left.ReloadEvents == 1 && left.RoundEvents == 1);
    notify(left, reload);
    expect("duplicate reload does not replay", left.ReloadEvents == 1 && left.RoundEvents == 1);
    const int nextReload = left.ReloadEvents, nextRound = left.RoundEvents;
    notify(left, ready);
    notify(left, reload);
    expect("new reload cycle plays again", left.ReloadEvents == nextReload+1 && left.RoundEvents == nextRound+1);
    notify(otherPlayer, reload);
    expect("other player retains third person reload", otherPlayer.RoundEvents == 1);
    notify(right, reload);
    expect("opposite parked launcher does not display a round", right.RoundEvents == 0);
    player.CurrentLauncher = &right;
    const int rightBefore = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("selected right starts its own reload", right.RoundEvents == rightBefore+1);
    player.CurrentHandsState = EPBHandsState::DeployedWeapon;
    player.FirstPerson = false;
    const int beforeSwitch = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("returning to gun ignores stale selection", right.RoundEvents == beforeSwitch);
    player.FirstPerson = true;
    player.Controller = nullptr;
    player.CurrentHandsState = EPBHandsState::UndeployingLauncherController;
    const int beforeStow = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("first person stow before controller update", right.RoundEvents == beforeStow);
    player.Controller = &local;
    player.CurrentHandsState = EPBHandsState::DeployingLauncherController;
    const int beforeDeploy = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("legitimate controller deploy reload", right.RoundEvents == beforeDeploy + 1);
    player.Living = false;
    const int beforeDeath = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("dead owner does not show round", right.RoundEvents == beforeDeath);
    player.Living = true;
    vehicle.RightLauncher = nullptr;
    const int beforeUnequip = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("removed launcher does not use stale selected pointer", right.RoundEvents == beforeUnequip);
    right.Pawn = &peer;
    const int beforeTransfer = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("old player slots cannot authorize new owner", right.RoundEvents == beforeTransfer);
    right.Pawn = nullptr;
    const auto orphanBefore = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("no owner leaves normal state intact", right.RoundEvents == orphanBefore && right.CurrentState == reload);
    APawn unrelated;
    right.Pawn = &unrelated;
    const auto nonCharacterBefore = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("unrelated pawn cannot provide equipment", right.RoundEvents == nonCharacterBefore);
    right.Pawn = &player;
    vehicle.RightLauncher = &right;
    player.Valid = false;
    const auto invalidBefore = right.RoundEvents;
    notify(right, ready);
    notify(right, reload);
    expect("invalid former owner cannot restore round", right.RoundEvents == invalidBefore);
    player.Valid = true;
    player.CurrentHandsState = EPBHandsState::DeployedWeapon;
    vehicle.RightLauncher = &auxiliary;
    player.CurrentLauncher = &left;
    auxiliary.Active = true;
    notify(auxiliary, reload);
    expect("active auxiliary reload alongside gun", auxiliary.RoundEvents == 1);
    auxiliary.Active = false;
    notify(auxiliary, ready);
    notify(auxiliary, reload);
    expect("inactive auxiliary parked in opposite slot", auxiliary.RoundEvents == 1);
    player.CurrentLauncher = &auxiliary;
    const int auxiliaryBefore = auxiliary.RoundEvents;
    notify(auxiliary, ready);
    notify(auxiliary, reload);
    expect("selected auxiliary reload after switch", auxiliary.RoundEvents == auxiliaryBefore+1);
    const int normalRoundBefore = auxiliary.RoundEvents;
    if (!HandleLauncherClientEvent(&auxiliary, nullptr, nullptr, "K2_ASingleAmmoReloaded"))
        auxiliary.K2_ASingleAmmoReloaded();
    expect("real per-round notification still reaches blueprint", auxiliary.RoundEvents == normalRoundBefore+1);
    expect("normal ammo and charges conserved", auxiliary.Magazine.AmmoInClip == 1 && auxiliary.Magazine.TotalAmmo == 9);
    return failures ? 1 : 0;
}

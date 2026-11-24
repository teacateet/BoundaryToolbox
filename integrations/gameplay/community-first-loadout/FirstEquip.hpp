#pragma once

namespace BrtFirstEquip
{
using namespace SDK;
using BrtDeathMenu::Clock;
using BrtDeathMenu::Time;

struct Opening
{
    APBCharacter* firstPawn{};
    APBWeapon* observed{};
    Time since{}, next{};
    APBWeapon* held{};
    Time heldSince{};
    bool finished{};
};
inline Opening opening;
inline thread_local bool reading{};

inline void Tick(UWorld* world, UPBLocalPlayer* local)
{
    const auto& room = BrtDeathMenu::room;
    if (reading || BrtDeathMenu::reading || opening.finished || room.playWorld != world ||
        room.localPlayer != local || !world->NetDriver || !room.connection ||
        room.connection != world->NetDriver->ServerConnection)
        return;
    const auto now = Clock::now();
    if (now < opening.next) return;
    opening.next = now + std::chrono::milliseconds(100);
    struct Guard { Guard() { reading = true; } ~Guard() { reading = false; } } guard;
    auto* pc = local->PlayerController;
    if (!pc || pc->Player != local || !pc->IsA(APBPlayerController::StaticClass()) ||
        !pc->IsLocalPlayerController() || !pc->PlayerState ||
        !pc->PlayerState->IsA(APBPlayerState::StaticClass())) return;
    auto* controller = static_cast<APBPlayerController*>(pc);
    auto* ps = static_cast<APBPlayerState*>(pc->PlayerState);
    auto* pawn = controller->GetPBCharacter();
    if (ps->GetDeaths() > 0)
    {
        opening.finished = true;
        return;
    }
    if (!pawn || pawn != pc->K2_GetPawn() || pawn->GetController() != pc) {
        opening.observed = nullptr;
        opening.held = nullptr;
        return;
    }
    if (!pawn->IsAlive() || pawn->IsInKilledStatus()) return;
    // 入场片段会暂时交还镜头。等回合真正开始，并等本人重新成为活动镜头。
    auto* gs = world->GameState;
    if (!gs || !gs->IsA(APBGameState::StaticClass()) || gs->GameModeClass.Get() != room.modeClass ||
        !static_cast<APBGameState*>(gs)->IsRoundInProgress() || pc->GetViewTarget() != pawn ||
        controller->bShowSelectRole || controller->IsInGameMenuOpened() || !pawn->IsFirstPerson())
    {
        opening.observed = nullptr;
        opening.held = nullptr;
        return;
    }
    if (opening.firstPawn && opening.firstPawn != pawn) {
        opening.finished = true;
        return;
    }
    // 入场镜头、占有复制或短暂的空 Pawn 都不是另一条命。仅在可玩的首命绑定。
    opening.firstPawn = pawn;
    if (pawn->CurrentWeapon || pawn->PendingWeapon ||
        pawn->CurrentHandsState != EPBHandsState::EmptyHand || pawn->PendingHandsState != EPBHandsState::EmptyHand)
    {
        opening.observed = nullptr;
        // 正在切枪或开场动画暂时把枪交到手上，不代表复制结束。只在持续持枪后结束观察。
        auto* held = pawn->CurrentWeapon;
        if (!held || pawn->CurrentHandsState != EPBHandsState::DeployedWeapon ||
            pawn->PendingHandsState != EPBHandsState::DeployedWeapon ||
            (pawn->PendingWeapon && pawn->PendingWeapon != held)) opening.held = nullptr;
        else if (opening.held != held) { opening.held = held; opening.heldSince = now; }
        else if (now - opening.heldSince >= std::chrono::seconds(2)) opening.finished = true;
        return;
    }
    opening.held = nullptr;
    auto* primary = pawn->Inventory.Num() ? pawn->Inventory[0] : nullptr;
    if (!primary || !UKismetSystemLibrary::IsValid(primary) || primary->GetPawnOwner() != pawn ||
        primary->TopOwner != pc || primary->bIsDropped)
    {
        opening.observed = nullptr;
        return;
    }
    if (opening.observed != primary)
    {
        opening.observed = primary;
        opening.since = now;
        return;
    }
    if (now - opening.since < std::chrono::milliseconds(800)) return;
    opening.finished = true;
    pawn->EquipPendingWeapon(primary);
    ClientDebugLog("[BRT-FIRST-EQUIP] Restored initial primary after inventory replication.");
}
}

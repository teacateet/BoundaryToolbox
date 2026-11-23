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
    if (ps->GetDeaths() > 0 || (opening.firstPawn && opening.firstPawn != pawn))
    {
        opening.finished = true;
        return;
    }
    if (!pawn || pawn != pc->K2_GetPawn() || pawn->GetController() != pc) return;
    opening.firstPawn = pawn;
    if (!pawn->IsAlive() || pawn->IsInKilledStatus()) return;
    // 入场片段会暂时交还镜头。等回合真正开始，并等本人重新成为活动镜头。
    auto* gs = world->GameState;
    if (!gs || !gs->IsA(APBGameState::StaticClass()) || gs->GameModeClass.Get() != room.modeClass ||
        !static_cast<APBGameState*>(gs)->IsRoundInProgress() || pc->GetViewTarget() != pawn ||
        controller->bShowSelectRole || controller->IsInGameMenuOpened() || !pawn->IsFirstPerson())
    {
        opening.observed = nullptr;
        return;
    }
    if (pawn->CurrentWeapon || pawn->PendingWeapon ||
        pawn->CurrentHandsState != EPBHandsState::EmptyHand || pawn->PendingHandsState != EPBHandsState::EmptyHand)
    {
        opening.finished = true;
        return;
    }
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

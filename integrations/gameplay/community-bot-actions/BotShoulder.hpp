#pragma once
#include "../SDK.hpp"
#include "../Logging/LogManager.h"
#include <array>
#include <chrono>
#include "BotBirthKit.hpp"

namespace brt::bot_shoulder {
using namespace SDK;
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
struct Identity {
    int index = 0, serial = 0;
    bool operator==(const Identity&) const = default;
};
inline Identity Id(UObject* value) {
    const auto ref = UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(value);
    return {ref.WeakPtr.ObjectIndex, ref.WeakPtr.ObjectSerialNumber};
}
struct Seat {
    Identity ai, pawn, held;
    Time next{}, release{}, seen{};
    int ammo = 0;
    bool outfitted = false;
};
inline bool Valid(UObject* o) { return o && UKismetSystemLibrary::IsValid(o); }
inline bool CanUse(APBLauncher* l) {
    if (!Valid(l) || !l->HasAuthority() || l->Magazine.AmmoInClip <= 0 ||
        l->bIsFiring || l->bPendingFiring ||
        (l->CurrentState != EPBLauncherState::Standby && l->CurrentState != EPBLauncherState::Ready)) return false;
    return brt::bot_birth::DirectPod(l->Class->GetName());
}
inline void Tick(UWorld* world) {
    static thread_local Time scan{};
    static thread_local Identity room{};
    static thread_local std::array<Seat,32> seats{};
    const auto now = Clock::now();
    if (now < scan || !world || !Valid(world->AuthorityGameMode) ||
        !world->AuthorityGameMode->IsA(APBGameMode::StaticClass())) return;
    scan = now + std::chrono::milliseconds(200);
    auto* mode = static_cast<APBGameMode*>(world->AuthorityGameMode);
    if (mode->BotControllers.Num() <= 0 || mode->BotControllers.Num() > int(seats.size())) return;
    const auto roomId = Id(mode);
    if (roomId.serial <= 0) return;
    if (!(room == roomId)) { seats = {}; room = roomId; }
    static const FName targetKey = UKismetStringLibrary::Conv_StringToName(L"TargetActor");
    static const FName aimKey = UKismetStringLibrary::Conv_StringToName(L"bIsNearlyAimed");
    for (int i = 0; i < mode->BotControllers.Num(); ++i) {
        auto* ai = mode->BotControllers[i];
        if (!Valid(ai) || !ai->HasAuthority()) continue;
        auto* base = ai->K2_GetPawn();
        if (!Valid(base) || !base->IsA(APBCharacter::StaticClass()) || base->GetController() != ai) continue;
        auto* pawn = static_cast<APBCharacter*>(base);
        auto* vehicle = pawn->CurrentVehicle;
        if (!pawn->IsAlive() || !Valid(vehicle) || vehicle->OwningCharacter != pawn) continue;
        const auto aiId = Id(ai), pawnId = Id(pawn);
        if (aiId.serial <= 0 || pawnId.serial <= 0) continue;
        // 控制器数组可重排，按弱对象序号保留每名 AI 的松开时刻与冷却。
        Seat* slot = nullptr;
        for (auto& candidate : seats) if (candidate.ai == aiId) { slot = &candidate; break; }
        if (!slot) for (auto& candidate : seats) if (!candidate.ai.serial || now - candidate.seen > std::chrono::seconds(30)) { slot=&candidate; break; }
        if (!slot) continue;
        if (!(slot->ai == aiId && slot->pawn == pawnId)) *slot = {aiId,pawnId,{},now+std::chrono::seconds(3)};
        slot->seen = now;
        const auto age=pawn->GetGameTimeSinceCreation();
        if(!slot->outfitted && age>=0.3f && age<=10.0f && pawn->CurrentWeapon && pawn->PBPlayerState && pawn->CharacterID!=FName{} &&
           (vehicle->LeftLauncher || vehicle->RightLauncher)) {
            slot->outfitted=true;
            brt::bot_birth::Apply(ai,pawn);
        }
        if (slot->held.serial) {
            if (now < slot->release) continue;
            for (auto* l : { vehicle->LeftLauncher, vehicle->RightLauncher })
                if (Valid(l) && Id(l) == slot->held) {
                    l->OnReleased();
                    ServerLog("[BRT-BOT-POD] released id=" + l->SavedData.ID.ToString() +
                        " clip-before=" + std::to_string(slot->ammo) + " clip-after=" + std::to_string(l->Magazine.AmmoInClip));
                    break;
                }
            slot->held = {};
            continue;
        }
        if (now < slot->next || !Valid(ai->Blackboard)) continue;
        auto* target = ai->Blackboard->GetValueAsObject(targetKey);
        if (!Valid(target) || !target->IsA(APBCharacter::StaticClass()) || !ai->Blackboard->GetValueAsBool(aimKey)) continue;
        auto* enemy = static_cast<APBCharacter*>(target);
        if (!enemy->IsAlive() || !pawn->IsEnemyForPawn(enemy) || !ai->LineOfSightTo(enemy, {}, false)) continue;
        for (auto* l : {vehicle->LeftLauncher,vehicle->RightLauncher}) if (CanUse(l) && l->GetOwnerPawn()==pawn) {
            auto id = Id(l);
            if (id.serial <= 0) continue;
            slot->held = id;
            slot->release = now + std::chrono::milliseconds(450);
            slot->next = now + std::chrono::seconds(18);
            slot->ammo = l->Magazine.AmmoInClip;
            l->OnPressed();
            break;
        }
    }
}
}

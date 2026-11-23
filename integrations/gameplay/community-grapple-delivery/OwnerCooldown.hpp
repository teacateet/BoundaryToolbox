#pragma once
#include "../SDK/Engine_parameters.hpp"
#include "../SDK/ProjectBoundary_classes.hpp"
#include <string>

#pragma push_macro("GetCommandLine")
#undef GetCommandLine

namespace brt::hook_delivery {
using namespace SDK;

inline FName Tag() {
    static const auto tag = UKismetStringLibrary::Conv_StringToName(L"BrtLocalHookReady");
    return tag;
}

inline void Send(APlayerController* pc, int deaths) {
    auto text = std::to_wstring(deaths);
    pc->ClientMessage(FString(text.c_str()), Tag(), 0);
}

inline bool Receive(UObject* obj, const std::string& event, void* args) {
    if (event != "Function Engine.PlayerController.ClientMessage" || !args) return false;
    const auto* msg = static_cast<const Params::PlayerController_ClientMessage*>(args);
    if (msg->Type != Tag()) return false;
    static const bool active = UKismetSystemLibrary::ParseParam(UKismetSystemLibrary::GetCommandLine(), L"BrtLocalPveUi");
    auto* world = UWorld::GetWorld();
    if (!active || !world || !world->NetDriver || !world->NetDriver->ServerConnection ||
        !obj || !obj->IsA(APBPlayerController::StaticClass()) ||
        world->NetDriver->ServerConnection->PlayerController != obj || !world->GameState ||
        !world->GameState->GameModeClass.Get()) return true;
    auto mode = world->GameState->GameModeClass->GetName();
    if (mode != "BP_PBGameMode_Rush_PVE_Normal_C" && mode != "BP_PBGameMode_Rush_PVE_Easy_C" &&
        mode != "BP_PBGameMode_Rush_PVE_Hard_C") return true;
    auto* pc = static_cast<APBPlayerController*>(obj);
    if (!pc->IsLocalPlayerController() || !pc->PlayerState || !pc->PlayerState->IsA(APBPlayerState::StaticClass())) return true;
    auto* ps = static_cast<APBPlayerState*>(pc->PlayerState);
    auto* base = pc->K2_GetPawn();
    if (msg->S.ToWString() != std::to_wstring(ps->GetDeaths()) || !base ||
        !base->IsA(APBCharacter::StaticClass()) || base->GetController() != pc) return true;
    auto* pawn = static_cast<APBCharacter*>(base);
    auto* gear = pawn->GetCurrentMobilityModule();
    if (!pawn->IsAlive() || !UKismetSystemLibrary::IsValid(gear) ||
        !gear->IsA(APBCatchingModule::StaticClass()) || gear->OwnerCharacter != pawn) return true;
    auto* hook = static_cast<APBCatchingModule*>(gear);
    if (hook->bIsInCoolDown) hook->ClientEndHookLockCoolDown();
    return true;
}
}

#pragma pop_macro("GetCommandLine")

#pragma once
#include "../SDK/Engine_parameters.hpp"
#include "../SDK/ProjectBoundary_classes.hpp"
#include "../SDK/ProjectBoundary_parameters.hpp"
#include "../BrtGrapple/Observer.hpp"
#include <string>

namespace brt::airlock {
using namespace SDK;
inline FName Tag() {
    static const auto tag = UKismetStringLibrary::Conv_StringToName(L"BrtLocalAirlock");
    return tag;
}

// 本机专服对同一次舱门交互可能在同一世界帧中处理两遍，第二遍会把刚打开的门关上。
inline bool DuplicateHatch(UObject* obj, const std::string& event, void* args) {
    if (event != "Function ProjectBoundary.PBAirLockHatch.MulticastInteractive" || !obj || !args) return false;
    auto* world = UWorld::GetWorld();
    const auto& room = brt::grapple::detail::session;
    auto* actor = static_cast<Params::PBAirLockHatch_MulticastInteractive*>(args)->Interactor;
    if (!world || !room.mode.Matches(world->AuthorityGameMode) || !room.pawn.Matches(actor)) return false;
    static UObject* lastDoor{};
    static UObject* lastPawn{};
    static UWorld* lastWorld{};
    static float lastTime{};
    const float now = UGameplayStatics::GetTimeSeconds(world);
    const bool duplicate = lastWorld == world && lastDoor == obj && lastPawn == actor && lastTime == now;
    lastDoor = obj;
    lastPawn = actor;
    lastWorld = world;
    lastTime = now;
    return duplicate;
}

// 局部关卡的广播可能未送达。只转送游戏已经执行的气闸状态变化。
inline void Publish(UObject* obj, const std::string& event, void* args) {
    char operation{};
    if (event.ends_with(".K2_IncreaseAirPressure")) operation = 'I';
    else if (event.ends_with(".K2_ReleaseAirPressure")) operation = 'R';
    else if (event.ends_with(".K2_OpenHatch")) operation = 'O';
    else if (event.ends_with(".K2_CloseHatch")) operation = 'C';
    else return;
    const bool hatch = operation == 'O' || operation == 'C';
    if (!obj || !obj->IsA(hatch ? APBAirLockHatch::StaticClass() : APBAirLockCenter::StaticClass())) return;
    auto* world = UWorld::GetWorld();
    if (!world || !world->NetDriver || !brt::grapple::detail::session.mode.Matches(world->AuthorityGameMode)) return;
    auto mode = world->AuthorityGameMode->Class->GetName();
    if (mode != "BP_PBGameMode_Rush_PVE_Normal_C" && mode != "BP_PBGameMode_Rush_PVE_Easy_C" &&
        mode != "BP_PBGameMode_Rush_PVE_Hard_C") return;
    auto path = UKismetSystemLibrary::GetPathName(obj).ToWString();
    std::wstring message(1, static_cast<wchar_t>(operation));
    message += path;
    for (auto* connection : world->NetDriver->ClientConnections) {
        if (connection && brt::grapple::detail::session.player.Matches(connection->PlayerController) &&
            connection->PlayerController->Player == connection)
            connection->PlayerController->ClientMessage(FString(message.c_str()), Tag(), 0);
    }
}

inline bool Receive(UObject* object, const std::string& event, void* args) {
    if (event != "Function Engine.PlayerController.ClientMessage" || !args) return false;
    auto* msg = static_cast<Params::PlayerController_ClientMessage*>(args);
    if (msg->Type != Tag()) return false;
    if (!object || !object->IsA(APBPlayerController::StaticClass()) ||
        !static_cast<APBPlayerController*>(object)->IsLocalPlayerController()) return true;
#pragma push_macro("GetCommandLine")
#undef GetCommandLine
    static const bool selected = UKismetSystemLibrary::ParseParam(UKismetSystemLibrary::GetCommandLine(), L"BrtLocalPveUi");
#pragma pop_macro("GetCommandLine")
    auto* world = UWorld::GetWorld();
    if (!selected || !world || !world->NetDriver || !world->NetDriver->ServerConnection ||
        world->NetDriver->ServerConnection->PlayerController != object || !world->GameState ||
        !world->GameState->GameModeClass.Get()) return true;
    const auto mode = world->GameState->GameModeClass->GetName();
    if (mode != "BP_PBGameMode_Rush_PVE_Normal_C" && mode != "BP_PBGameMode_Rush_PVE_Easy_C" &&
        mode != "BP_PBGameMode_Rush_PVE_Hard_C") return true;
    const auto text = msg->S.ToWString();
    if (text.size() < 2 || text.size() > 512) return true;
    const auto op = text[0];
    const bool hatch = op == L'O' || op == L'C';
    if (!hatch && op != L'I' && op != L'R') return true;
    TArray<AActor*> matches;
    UGameplayStatics::GetAllActorsOfClass(world, hatch ? APBAirLockHatch::StaticClass() : APBAirLockCenter::StaticClass(), &matches);
    const auto path = text.substr(1);
    for (auto* actor : matches) {
        if (!UKismetSystemLibrary::IsValid(actor) || UKismetSystemLibrary::GetPathName(actor).ToWString() != path) continue;
        if (hatch) {
            auto* door = static_cast<APBAirLockHatch*>(actor);
            if (op == L'C' && door->bIsOpen) door->CloseHatch();
            if (op == L'O' && !door->bIsOpen && UKismetSystemLibrary::IsValid(door->AirLockCenter))
                door->OpenHatch(door->AirLockCenter->PressureLevel);
        } else {
            auto* center = static_cast<APBAirLockCenter*>(actor);
            auto state = center->AirLockState;
            if (op == L'I' && state != EPBAirLockState::INCREASE && state != EPBAirLockState::FULL)
                center->IncreaseAirPressure();
            if (op == L'R' && state != EPBAirLockState::RELEASE && state != EPBAirLockState::EMPTY)
                center->ReleaseAirPressure(false);
        }
        break;
    }
    return true;
}
}

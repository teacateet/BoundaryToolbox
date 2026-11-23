#pragma once

#include "DeathMenuTravel.hpp"
#include "DeathMenu.sdk.hpp"

namespace BrtDeathMenuV084
{
// 此入口只在角色原有的 ReceiveTick 上执行。一个房间里的角色共用本地视口。
inline void CharacterTick()
{
    using namespace BrtDeathMenu;
    if (reading)
        return;
    if (auto issued = BrtDeathMenuTravel::Take())
    {
        ReadingScope scope;
        const bool selected = issued->source && SDK::UKismetSystemLibrary::ParseParam(
            SDK::UKismetSystemLibrary::GetCommandLine(), L"BrtLocalPveUi");
        room.Travel(selected, issued->target, issued->source, issued->instance, issued->player, issued->issued);
        gesture = {};
        nextSample = {};
    }
    if (!room.origin || Clock::now() < nextSample)
        return;
    auto* world = SDK::UWorld::GetWorld();
    if (!world || !world->OwningGameInstance || world->OwningGameInstance->LocalPlayers.Num() != 1)
    {
        gesture = {};
        return;
    }
    auto* local = world->OwningGameInstance->LocalPlayers[0];
    if (!local || local != room.localPlayer)
    {
        room = {};
        gesture = {};
        return;
    }
    Tick(world, static_cast<SDK::UPBLocalPlayer*>(local));
}
}

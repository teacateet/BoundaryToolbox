#pragma once
#include "../Client/DeathMenu.hpp"
#include "../SDK/ProjectBoundary_classes.hpp"
#include <algorithm>

namespace brt::redeploy {
using namespace SDK;
using Clock = std::chrono::steady_clock;
struct Pending {
    UWorld* world{};
    APBGameMode* mode{};
    FName role{};
    int deaths{};
    Clock::time_point ready{}, expires{};
    bool inactive{};
};

// 入口仅由档案模块在校验本机房主、连接、Steam 游戏版本和本局干员名单后调用。
inline void Confirm(Pending& p, APBPlayerState& state, FName role) {
    if (!state.IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death) || state.GetDeaths() <= 0 || role == FName{}) {
        p = {}; return;
    }
    auto* world = UWorld::GetWorld();
    auto* gm = world ? world->AuthorityGameMode : nullptr;
    if (!gm || !gm->IsA(APBGameMode::StaticClass()) ||
        !BrtDeathMenu::IsPveClass(UKismetSystemLibrary::GetPathName(gm->Class).ToString())) { p = {}; return; }
    if (p.world == world && p.role == role && p.deaths == state.GetDeaths()) return;
    const auto now = Clock::now();
    p = {world, static_cast<APBGameMode*>(gm), role, state.GetDeaths(), now + std::chrono::seconds(8),
        now + std::chrono::seconds(35)};
}

inline void Tick(Pending& p, APBPlayerController& pc, APBPlayerState& state, bool sameOwner) {
    if (!p.world) return;
    auto* world = UWorld::GetWorld();
    const auto now = Clock::now();
    if (!sameOwner || world != p.world || world->AuthorityGameMode != p.mode || now >= p.expires ||
        !state.IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death) || state.GetDeaths() != p.deaths ||
        state.bOnlySpectator || !p.mode->HasMatchStarted() || p.mode->HasMatchEnded() || !p.mode->IsRoundInProgress()) {
        p = {}; return;
    }
    static const auto inactive = UKismetStringLibrary::Conv_StringToName(L"Inactive");
    if (pc.StateName != inactive || pc.K2_GetPawn() || !(p.role == state.SelectedCharacterID)) return;
    if (!p.inactive) {
        p.inactive = true;
        p.ready = (std::max)(p.ready, now + std::chrono::seconds(3));
    }
    if (now < p.ready) return;
    p.ready = now + std::chrono::milliseconds(500);
    if (!p.mode->PlayerCanRestart(&pc)) return;
    auto* gm = p.mode;
    p = {}; // 在进入原生回调前消耗本次部署，不能重复生成。
    gm->RestartPlayer(&pc);
}
}

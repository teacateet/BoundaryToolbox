#pragma once
#include "Timeline.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <string_view>

namespace brt::grapple {
namespace detail {
using namespace SDK;

struct Identity {
    const UObject* address = nullptr;
    FName name{};
    static Identity Of(const UObject* current) { return current ? Identity{current, current->Name} : Identity{}; }
    bool Matches(const UObject* current) const { return current && address == current && name == current->Name; }
};

inline bool Supported(std::string_view mode, std::string_view map) {
    if (mode == "BP_PBGameMode_Rush_PVE_Easy_C") return map == "Warehouse" || map == "DataCenter";
    if (mode == "BP_PBGameMode_Rush_PVE_Normal_C") return map == "MiniFarm" || map == "CircularX" || map == "OSS";
    return mode == "BP_PBGameMode_Rush_PVE_Hard_C" && (map == "OSS" || map == "CircularX");
}

struct Session {
    Identity mode, player, state, pawn, gear;
    std::string verifiedOwner;
    int playerId = 0, deaths = -1;
    unsigned owner = 0, life = 0, slot = 0, lines = 0;
    float lifeStart = 0;
    Timeline timeline;
    CooldownDelivery delivery;

    void Log(const char* event, int ms, const Sample& sample) {
        if (lines >= 193) return;
        if (lines == 192) event = "limit";
        ++lines;
        std::printf("[BRT-GRAPPLE] owner=%u life=%u slot=%u ms=%d event=%s hook=%u cool=%u anchor=%u permille=%d\n",
            owner, life, slot, ms, event, sample.phase, unsigned(sample.cooling), unsigned(sample.anchor), sample.fraction);
        std::fflush(stdout);
    }
    int Age(float now) const {
        return static_cast<int>(std::clamp((now - lifeStart) * 1000.0f, 0.0f, 86400000.0f));
    }
    void CloseSlot(int elapsed, const char* reason) {
        if (gear.address && timeline.hasSample) Log(reason, elapsed, timeline.previous);
        gear = {};
        timeline = {};
        delivery = {};
    }
    void CloseLife(int elapsed) {
        CloseSlot(elapsed, "life-end");
        pawn = {};
    }
};
inline Session session;
inline unsigned generation = 0;

// 服务端只把通知发给当前装备的实际拥有连接。不会给没有拥有者的 Actor 重绑所有权。
inline bool CanNotify(SDK::APBCatchingModule* hook, SDK::APBCharacter* pawn, SDK::APlayerController* pc) {
    if (!SDK::UKismetSystemLibrary::IsValid(pc->NetConnection)) return false;
    auto* owner = hook->GetOwner();
    for (unsigned depth = 0; depth < 8 && SDK::UKismetSystemLibrary::IsValid(owner); ++depth) {
        if (owner == pawn || owner == pc) return true;
        if (owner->IsA(SDK::APawn::StaticClass()) || owner->IsA(SDK::AController::StaticClass())) return false;
        owner = owner->GetOwner();
    }
    return false;
}

inline void LogDelivery(const char* event, int age) noexcept {
    try {
        static unsigned rows = 0;
        if (rows >= 64) return;
        ++rows;
        static std::ofstream file = [] {
            std::error_code error;
            std::filesystem::create_directory("logs", error);
            return std::ofstream("logs/BRT-grapple-sync-" + std::to_string(_getpid()) + ".log", std::ios::trunc);
        }();
        if (file) {
            file << "[BRT-GRAPPLE-SYNC] owner=" << session.owner << " life=" << session.life
                 << " slot=" << session.slot << " ms=" << age << " event=" << event << '\n';
            file.flush();
        }
    } catch (...) {}
}
}

// 调用点在本机房间服务已核实此登录身份、返回完整房主选角快照之后。
inline void BindVerified(SDK::AGameMode* mode, SDK::APBPlayerController* controller, const std::string& owner) {
    using namespace detail;
    if (!mode || !controller || !controller->PlayerState ||
        !controller->PlayerState->IsA(APBPlayerState::StaticClass()) || !mode->Class ||
        !mode->HasAuthority() || !controller->HasAuthority() || !UKismetSystemLibrary::IsDedicatedServer(mode)) return;
    const auto map = UGameplayStatics::GetCurrentLevelName(mode, true).ToString();
    if (!Supported(mode->Class->GetName(), map)) return;
    if (session.mode.Matches(mode) && session.player.Matches(controller) &&
        session.state.Matches(controller->PlayerState) && session.verifiedOwner == owner) return;
    session = {};
    if (generation == 999999) return;
    session.mode = Identity::Of(mode);
    session.player = Identity::Of(controller);
    session.state = Identity::Of(controller->PlayerState);
    session.playerId = controller->PlayerState->PlayerId;
    session.verifiedOwner = owner;
    session.owner = ++generation;
}

inline bool Bound() { return detail::session.mode.address != nullptr; }

inline void Tick(SDK::APBGameMode* gameMode, SDK::APBGameState* gameState) noexcept {
    using namespace detail;
    try {
        if (!Bound()) return;
        if (!session.mode.Matches(gameMode) || !gameMode->HasAuthority()) { session = {}; return; }
        const float now = UGameplayStatics::GetTimeSeconds(gameMode);
        if (!std::isfinite(now)) return;
        const int elapsed = session.Age(now);
        if (!gameState || !gameState->IsRoundInProgress() || now < session.lifeStart) {
            session.CloseLife(elapsed);
            session.lifeStart = now;
            return;
        }
        // 从当前游戏世界重新取得玩家。保存的地址仅用来比对，从不解引用。
        auto* current = UGameplayStatics::GetPlayerController(gameMode, 0);
        if (!session.player.Matches(current) || !session.state.Matches(current->PlayerState) ||
            current->PlayerState->PlayerId != session.playerId || !current->HasAuthority()) {
            session.CloseLife(elapsed);
            return;
        }
        auto* base = current->K2_GetPawn();
        if (!base || !base->IsA(APBCharacter::StaticClass()) || base->GetController() != current) {
            session.CloseLife(elapsed);
            return;
        }
        auto* pawn = static_cast<APBCharacter*>(base);
        if (!pawn->HasAuthority() || !pawn->IsAlive()) { session.CloseLife(elapsed); return; }
        auto* state = static_cast<APBPlayerState*>(current->PlayerState);
        const int deaths = state->GetDeaths();
        if (!session.pawn.Matches(pawn) || session.deaths != deaths) {
            session.CloseLife(elapsed);
            if (state->GetDefaultIDStr().ToString() != session.verifiedOwner || session.life == 999999) {
                session = {};
                return;
            }
            session.pawn = Identity::Of(pawn);
            session.deaths = deaths;
            ++session.life;
            session.slot = 0;
            session.lines = 0;
            session.lifeStart = now;
        }
        const int age = session.Age(now);
        auto* mobility = pawn->GetCurrentMobilityModule();
        if (!mobility || !mobility->IsA(APBCatchingModule::StaticClass()) || mobility->OwnerCharacter != pawn ||
            !mobility->HasAuthority()) { session.CloseSlot(age, "slot-end"); return; }
        if (!session.gear.Matches(mobility)) {
            session.CloseSlot(age, "slot-end");
            if (session.slot == 999999) return;
            ++session.slot;
            session.gear = Identity::Of(mobility);
        }
        auto* grapple = static_cast<APBCatchingModule*>(mobility);
        const auto phase = static_cast<unsigned>(grapple->CurrentHookStatus);
        if (phase > 3) { session.CloseSlot(age, "slot-end"); return; }
        const auto& lock = grapple->CurrentHookLockData;
        const bool anchor = phase == 2 && (lock.CaughtObjectType != ECatchedObjectType::None &&
            lock.CaughtObjectType < ECatchedObjectType::ECatchedObjectType_MAX);
        const Sample sample{phase, bool(grapple->bIsInCoolDown), anchor, Fraction(grapple->GetCoolingDownPercent())};
        session.timeline.Observe(age, sample, [age](const char* event, const Sample& value) {
            session.Log(event, age, value);
        });
        auto delivery = session.delivery.Observe(sample, [&] { return CanNotify(grapple, pawn, current); });
        if (delivery == Delivery::NotifyOwner) {
            grapple->ClientEndHookLockCoolDown();
            LogDelivery("owner-rpc-called", age);
        } else if (delivery == Delivery::WaitForOwner) {
            LogDelivery("owner-route-missing", age);
        }
    } catch (const std::exception&) { session = {}; }
}
}

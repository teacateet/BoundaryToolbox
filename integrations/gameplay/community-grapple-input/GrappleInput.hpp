#pragma once
#include "MarkedHeadRadar.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace BrtGrappleInput {
using namespace SDK;
using BrtSparrowReticle::Event;
using Weak = BrtSparrowReticle::Instance;

// 只处理角色原有的界面通知；所有引擎查询重入此入口时会直接返回。
inline const char* NativeEvent(std::string_view name) {
    if (Event(name, "K2_PrepareToLaunchHook")) return "prepare";
    if (Event(name, "K2_CancelPreparationOfLaunchHook")) return "cancel";
    if (Event(name, "K2_LaunchHook")) return "launch";
    if (Event(name, "K2_ResetHook")) return "reset";
    return nullptr;
}

struct Journal {
    unsigned rows = 0;
    std::ofstream file;
    void Write(const char* line) {
        if (rows > 256) return;
        if (rows == 0) {
            auto saved = UKismetSystemLibrary::GetProjectSavedDirectory().ToWString();
            if (!saved.empty()) {
                auto logs = std::filesystem::path(saved) / L"Logs";
                std::error_code error;
                std::filesystem::create_directories(logs, error);
                if (!error) file.open(logs / L"BRT-grapple-input.log", std::ios::trunc);
            }
        }
        if (rows++ == 256) line = "[BRT-GRAPPLE-INPUT] event=limit";
        if (file.is_open()) {
            file << line << '\n';
            file.flush();
        }
        ClientLog(line);
    }
};

struct Keys {
    std::vector<FKey> keys;
    long long frame = -1;
    unsigned seen = 0;
    int mapped = -1;
    bool down = false;

    // 读取当前玩家的选项，每一帧最多取一次映射，不假定键鼠或手柄的具体按键。
    void Refresh(long long now) {
        if (frame == now) return;
        frame = now;
        seen = 0;
        mapped = -1;
        keys.clear();
        auto* subsystem = USubsystemBlueprintLibrary::GetEngineSubsystem(UPBOptions::StaticClass());
        if (!UKismetSystemLibrary::IsValid(subsystem)) return;
        auto* options = static_cast<UPBOptions*>(subsystem);
        static const auto action = UKismetStringLibrary::Conv_StringToName(FString(L"ActivateMobility"));
        const auto actual = options->GetKeysFromActionName(action);
        for (int i = 0; i < actual.Num(); ++i) {
            const auto& key = actual[i];
            if (!UKismetInputLibrary::Key_IsValid(key)) continue;
            if (std::none_of(keys.begin(), keys.end(), [&](const FKey& other){ return other.KeyName == key.KeyName; }))
                keys.push_back(key);
        }
        mapped = static_cast<int>(keys.size());
    }

    // 同一帧内可能先到 Tick，再到原生准备通知，两个边沿各记一次。
    unsigned Edges(APlayerController* pc) {
        unsigned edges = 0;
        down = false;
        for (const auto& key : keys) {
            if (pc->WasInputKeyJustPressed(key)) edges |= 1;
            if (pc->WasInputKeyJustReleased(key)) edges |= 2;
            if (pc->IsInputKeyDown(key)) down = true;
        }
        const auto fresh = edges & ~seen;
        seen |= edges;
        return fresh;
    }
};

struct View {
    std::optional<Weak> pawn, controller, gear;
    Keys keys;
    unsigned life = 0, slot = 0, press = 0;
    int deaths = -1, lastMapped = -2;
    bool closed = true, sawDead = false;
    bool Matches(APBCharacter* actor) const { return pawn && pawn->Resolve() == actor; }

    void Start(APBCharacter* actor, APlayerController* pc, int count) {
        pawn.emplace(actor);
        controller.emplace(pc);
        gear.reset();
        keys = {};
        deaths = count;
        lastMapped = -2;
        closed = false;
        sawDead = false;
        ++life;
        slot = 0;
        press = 0;
    }
    void Record(Journal& journal, APBCharacter* actor, const char* event, long long frame, unsigned edge) {
        auto* current = actor->GetCurrentMobilityModule();
        if (!UKismetSystemLibrary::IsValid(current)) current = nullptr;
        if (current && (!gear || gear->Resolve() != current)) { gear.emplace(current); ++slot; }
        if (!current) gear.reset();
        const char* kind = current ? "other" : "none";
        int phase = -1, cooling = -1, enabled = -1, activated = -1;
        if (current && current->IsA(APBCatchingModule::StaticClass())) {
            kind = "foreign";
            if (current->OwnerCharacter == actor) {
                kind = "grapple";
                auto* hook = static_cast<APBCatchingModule*>(current);
                phase = static_cast<int>(hook->CurrentHookStatus);
                cooling = hook->bIsInCoolDown;
                enabled = current->bIsEnabled;
                activated = current->IsActivated();
            }
        }
        char line[360]{};
        std::snprintf(line, sizeof(line),
            "[BRT-GRAPPLE-INPUT] life=%u frame=%lld press=%u event=%s edge=%u down=%u mapped=%d slot=%u module=%s phase=%d cool=%d enabled=%d active=%d cooled=%u allowed=%u using=%u",
            life, frame, press, event, edge, unsigned(keys.down), keys.mapped, current ? slot : 0, kind,
            phase, cooling, enabled, activated, unsigned(actor->GetHasCatchingCooledDown()),
            unsigned(actor->GetIsAllowedCatch()), unsigned(actor->IsInHookUsingState()));
        journal.Write(line);
    }
};

inline void OnCharacter(APBCharacter* actor, std::string_view name) {
    const char* native = NativeEvent(name);
    const bool end = Event(name, "NotifyBeKilled") || Event(name, "ReceiveUnpossessed") || Event(name, "ReceiveEndPlay");
    const bool possess = Event(name, "ReceivePossessed") || Event(name, "K2_OnControllerReplicatedPossess");
    if (!actor || (!end && !possess && !native && !Event(name, "ReceiveTick"))) return;
    static View view;
    static Journal journal;
    if (journal.rows > 256) return;
    if (end) {
        if (view.Matches(actor)) view.closed = true;
        return;
    }
    auto* controller = actor->GetController();
    if (!controller || !controller->IsLocalPlayerController()) return;
    auto* pc = static_cast<APlayerController*>(controller);
    if (pc->K2_GetPawn() != actor || !BrtHeadRadar::InScope(actor)) return;
    const bool same = view.Matches(actor) && view.controller && view.controller->Resolve() == pc;
    if (!actor->IsAlive() || actor->IsInKilledStatus()) {
        if (same) { view.closed = true; view.sawDead = true; }
        return;
    }
    auto* state = pc->PlayerState;
    const int deaths = UKismetSystemLibrary::IsValid(state) && state->IsA(APBPlayerState::StaticClass())
        ? static_cast<APBPlayerState*>(state)->GetDeaths() : -1;
    const bool newCount = same && view.deaths >= 0 && deaths >= 0 && deaths != view.deaths;
    if (same && view.closed && !possess && !view.sawDead && !newCount) return;
    const bool start = !same || view.closed || newCount;
    if (start) view.Start(actor, pc, deaths);
    if (deaths >= 0) view.deaths = deaths;
    const long long frame = UKismetSystemLibrary::GetFrameCount();
    view.keys.Refresh(frame);
    const unsigned edges = view.keys.Edges(pc);
    if (edges & 1u) ++view.press;
    if (start || view.lastMapped != view.keys.mapped)
        view.Record(journal, actor, start ? "scope" : "mapping", frame, 0);
    view.lastMapped = view.keys.mapped;
    if (edges) view.Record(journal, actor, "mapped-key", frame, edges);
    if (native) view.Record(journal, actor, native, frame, 0);
}
}

#pragma once

#include "RoomHttp.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include "SDK/ProjectBoundary_parameters.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <windows.h>

namespace brt::birth_trace {
namespace detail {
enum class Event { None, Restart, WeaponConfig, ConfirmRole, FieldMod, AddWeapon, InitWeapon, Born };

inline Event Identify(std::string_view name) noexcept {
    if (name == "Function ProjectBoundary.PBFieldModManager.OnRestartInStartSpot") return Event::Restart;
    if (name == "Function ProjectBoundary.PBFieldModManager.GetWeaponNetworkConfig") return Event::WeaponConfig;
    if (name == "Function ProjectBoundary.PBPlayerController.ServerConfirmRoleSelection") return Event::ConfirmRole;
    if (name == "Function ProjectBoundary.PBPlayerState.ClientInitFieldMod") return Event::FieldMod;
    if (name == "Function ProjectBoundary.PBCharacter.AddWeapon") return Event::AddWeapon;
    if (name == "Function ProjectBoundary.PBWeapon.InitWeapon") return Event::InitWeapon;
    if (name == "Function ProjectBoundary.PBCharacter.NotifyBorn") return Event::Born;
    return Event::None;
}

inline const char* Label(Event event) noexcept {
    switch (event) {
    case Event::Restart: return "restart-in-start-spot";
    case Event::WeaponConfig: return "weapon-network-config";
    case Event::ConfirmRole: return "confirm-role";
    case Event::FieldMod: return "client-init-field-mod";
    case Event::AddWeapon: return "add-weapon";
    case Event::InitWeapon: return "init-weapon";
    case Event::Born: return "notify-born";
    default: return "none";
    }
}

inline std::atomic<unsigned> lines{}, calls{}, owners{}, unownedQueries{};
inline std::atomic_flag announced = ATOMIC_FLAG_INIT;
inline std::mutex outputMutex;
inline thread_local bool inspecting = false;
struct Inspection {
    bool previous = inspecting;
    Inspection() noexcept { inspecting = true; }
    ~Inspection() { inspecting = previous; }
};

// 只检查本进程已取得的房间环境；不调用任何 HTTP 方法，也不输出凭据。
inline bool Enabled() noexcept {
    static const bool enabled = [] {
        try { return spawn::RoomHttp::FromLocalServer().has_value(); }
        catch (...) { return false; }
    }();
    return enabled;
}

struct Context {
    SDK::APBPlayerController* controller{};
    const SDK::UObject* manager{};
    unsigned owner{}, call{}, depth{};
};
inline thread_local Context current;
struct Identity {
    const SDK::APBPlayerController* controller{};
    SDK::FName name{};
    unsigned number{};
};
inline thread_local std::array<Identity, 64> identities{};

inline SDK::APBPlayerController* Player(SDK::UObject* object) {
    if (!object || !object->IsA(SDK::APBPlayerController::StaticClass())) return nullptr;
    auto* player = static_cast<SDK::APBPlayerController*>(object);
    if (!player->HasAuthority() || !player->PlayerState || !player->NetConnection ||
        player->NetConnection->PlayerController != player) return nullptr;
    return player;
}

inline unsigned Number(SDK::APBPlayerController* player) noexcept {
    for (const auto& entry : identities)
        if (entry.controller == player && entry.name == player->Name) return entry.number;
    for (auto& entry : identities) {
        if (entry.controller) continue;
        entry = {player, player->Name, owners.fetch_add(1, std::memory_order_relaxed) + 1};
        return entry.number;
    }
    return 0;
}

inline void Log(const char* phase, Event event, const Context& context, unsigned parent,
                bool returned, int parts = -1, int slots = -1) noexcept {
    try {
        const std::lock_guard<std::mutex> lock(outputMutex);
        const unsigned line = lines.fetch_add(1, std::memory_order_relaxed);
        if (line >= 100) return;
        char message[320]{};
        if (line == 99) {
            std::snprintf(message, sizeof(message), "[BRT-BIRTH-TRACE] line=100 phase=limit\n");
        } else {
            std::snprintf(message, sizeof(message),
                "[BRT-BIRTH-TRACE] line=%u phase=%s event=%s owner=%u call=%u parent=%u depth=%u returned=%u parts=%d slots=%d\n",
                line + 1, phase, Label(event), context.owner, context.call, parent, context.depth,
                unsigned(returned), parts, slots);
        }
        std::fputs(message, stdout);
        std::fflush(stdout);
        // 相对专服工作目录写入，绕过启动器的 stdout 白名单；失败不影响原调用。
        static std::ofstream file = [] {
            std::error_code error;
            std::filesystem::create_directory("logs", error);
            return std::ofstream("logs/BRT-birth-trace-" + std::to_string(GetCurrentProcessId()) + ".log",
                std::ios::out | std::ios::app);
        }();
        if (file.is_open()) {
            file << message;
            file.flush();
        }
    } catch (...) {}
}
} // namespace detail

// 覆盖原 ProcessEvent 调用的栈作用域；仅采样期间抑制重入，不屏蔽游戏自己的嵌套事件。
class Scope {
    detail::Event event_ = detail::Event::None;
    detail::Context previous_{}, context_{};
    void* parameters_{};
    bool active_{}, returned_{};

public:
    Scope(SDK::UObject* object, std::string_view functionName, void* parameters) noexcept {
        using namespace detail;
        if (inspecting || lines.load(std::memory_order_relaxed) >= 100) return;
        Inspection inspection;
        if (!Enabled()) return;
        if (!announced.test_and_set(std::memory_order_relaxed)) Log("armed", Event::None, {}, 0, false);
        event_ = Identify(functionName);
        if (event_ == Event::None || !object || (!parameters && event_ != Event::Born)) return;
        try {
            SDK::APBPlayerController* player = nullptr;
            const SDK::UObject* manager = current.manager;
            switch (event_) {
            case Event::Restart:
                if (!object->IsA(SDK::UPBFieldModManager::StaticClass())) return;
                player = Player(static_cast<SDK::Params::PBFieldModManager_OnRestartInStartSpot*>(parameters)->InController);
                manager = object;
                break;
            case Event::WeaponConfig:
                if (!object->IsA(SDK::UPBFieldModManager::StaticClass())) return;
                // 只有相同管理器的同步调用才沿用当前拥有者。
                if (current.manager == object) player = current.controller;
                if (!player && unownedQueries.fetch_add(1, std::memory_order_relaxed) >= 4) return;
                break;
            case Event::ConfirmRole:
                player = Player(object);
                manager = nullptr;
                break;
            case Event::FieldMod:
                if (!object->IsA(SDK::APBPlayerState::StaticClass())) return;
                player = Player(static_cast<SDK::APBPlayerState*>(object)->GetOwner());
                break;
            case Event::AddWeapon:
            case Event::Born:
                if (!object->IsA(SDK::APBCharacter::StaticClass())) return;
                player = Player(static_cast<SDK::APBCharacter*>(object)->GetController());
                break;
            case Event::InitWeapon:
                if (!object->IsA(SDK::APBWeapon::StaticClass())) return;
                if (auto* pawn = static_cast<SDK::APBWeapon*>(object)->GetPawnOwner())
                    player = Player(pawn->GetController());
                break;
            default: return;
            }
            if (!player && event_ != Event::WeaponConfig) return;
            if (player != current.controller && event_ != Event::Restart) manager = nullptr;
            previous_ = current;
            context_ = {player, manager, player ? Number(player) : 0,
                calls.fetch_add(1, std::memory_order_relaxed) + 1, previous_.depth + 1};
            parameters_ = parameters;
            current = context_;
            active_ = true;
            int parts = -1, slots = -1;
            if (event_ == Event::InitWeapon) {
                const auto& config = static_cast<SDK::Params::PBWeapon_InitWeapon*>(parameters)->InPartSaved;
                parts = config.WeaponPartConfigs.Num();
                slots = config.WeaponPartSlotTypeArray.Num();
            }
            Log("enter", event_, context_, previous_.call, false, parts, slots);
        } catch (...) {
            // 采样失败不改变原始调用；已建立的作用域仍由析构恢复。
        }
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    void OriginalReturned() noexcept {
        using namespace detail;
        if (!active_ || returned_) return;
        Inspection inspection;
        returned_ = true;
        int parts = -1, slots = -1;
        if (event_ == Event::WeaponConfig) {
            const auto& config = static_cast<SDK::Params::PBFieldModManager_GetWeaponNetworkConfig*>(parameters_)->ReturnValue;
            parts = config.WeaponPartConfigs.Num();
            slots = config.WeaponPartSlotTypeArray.Num();
        }
        Log("native-return", event_, context_, previous_.call, true, parts, slots);
    }

    ~Scope() {
        using namespace detail;
        if (!active_) return;
        Inspection inspection;
        current = previous_;
        // 原调用可能销毁实体；此处只输出进入时保存的数字，不再解引用实体或参数。
        Log("exit", event_, context_, previous_.call, returned_);
    }
};
} // namespace brt::birth_trace

#pragma once
#include "PreorderGate.hpp"
#include "NativeRoleTable.hpp"
#include "Reconnect.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <type_traits>

namespace brt::preorder {
namespace detail {
using namespace SDK;
using brt::reconnect::Identity;
struct Key {
    Identity controller, transport, state;
    bool operator==(const Key& other) const {
        return controller == other.controller && transport == other.transport && state == other.state;
    }
};
using Clock = std::chrono::steady_clock;
struct Pending {
    Key key;
    spawn::RoomHttp room;
    spawn::Member member;
    std::vector<spawn::SavedMenuRole> rows;
    Clock::time_point expires;
    const char* waiting{};
};
inline Gate<Key, native::RoleKey> gate;
inline std::shared_ptr<Pending> pending;
inline thread_local bool ticking{};

inline void Log(const char* event, const char* reason) noexcept {
    try {
        static unsigned lines{};
        if (lines >= 32) return;
        ++lines;
        char message[192]{};
        std::snprintf(message, sizeof(message), "[BRT-PREORDER] event=%s reason=%s\n", event, reason);
        std::fputs(message, stdout);
        std::fflush(stdout);
        static std::ofstream file = [] {
            std::error_code error;
            std::filesystem::create_directory("logs", error);
            return std::ofstream("logs/BRT-preorder-" + std::to_string(GetCurrentProcessId()) + ".log", std::ios::app);
        }();
        if (file.is_open()) { file << message; file.flush(); }
    } catch (...) {}
}
inline void Wait(const std::shared_ptr<Pending>& item, const char* reason) {
    if (item->waiting != reason) { item->waiting = reason; Log("waiting", reason); }
}
inline bool Read(std::uintptr_t source, void* destination, std::size_t bytes) {
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(source), destination, bytes, &copied) && copied == bytes;
}
inline bool SupportedImage() {
    static const bool supported = native::SupportedImage(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)), Read);
    return supported;
}
inline std::optional<Key> KeyFor(APBPlayerController* pc, APBPlayerState* state) {
    if (!pc || !state || !pc->HasAuthority() || pc->IsActorBeingDestroyed() || pc->PlayerState != state ||
        !pc->NetConnection || !UKismetSystemLibrary::IsValid(pc->NetConnection) || pc->NetConnection->PlayerController != pc) return {};
    Key key{Identity(pc), Identity(pc->NetConnection), Identity(state)};
    if (!key.controller.Valid() || !key.transport.Valid() || !key.state.Valid()) return {};
    return key;
}
inline FName Name(const std::string& value) {
    const std::wstring wide(value.begin(), value.end());
    return UKismetStringLibrary::Conv_StringToName(FString(wide.c_str()));
}
inline native::RoleKey RoleKey(const FName& name) {
    static_assert(sizeof(FName) == sizeof(native::RoleKey) && std::is_trivially_copyable_v<FName>);
    native::RoleKey key{};
    std::memcpy(key.data(), &name, key.size());
    return key;
}
inline void Finish(const std::shared_ptr<Pending>& item) {
    if (auto* login = reconnect::connections.VerifiedEntry(item->member.connection)) login->menuHandled = true;
    if (pending == item) { pending.reset(); gate.Cancel(); }
}
}

inline bool HasPending(SDK::APBPlayerController* pc, SDK::APBPlayerState* state) {
    const auto key = detail::KeyFor(pc, state);
    return key && detail::pending && detail::pending->key == *key;
}

inline bool Queue(SDK::APBPlayerController* pc, SDK::APBPlayerState* state, const spawn::RoomHttp& room,
                  const spawn::Member& member, const std::vector<spawn::SavedMenuRole>& rows) {
    const auto key = detail::KeyFor(pc, state);
    if (!key || rows.size() != 6 || !login::ValidRows(rows)) return false;
    if (detail::pending && detail::pending->key == *key) return true;
    std::array<native::RoleKey, 6> roles{};
    for (std::size_t i = 0; i < roles.size(); ++i) roles[i] = detail::RoleKey(detail::Name(rows[i].id));
    // 60 秒只是放弃本次预订的上限，不是解锁条件；每次提交仍要求实际原生表中的键。
    detail::pending = std::make_shared<detail::Pending>(detail::Pending{*key, room, member, rows,
        detail::Clock::now() + std::chrono::seconds(60)});
    detail::gate.Queue(*key, roles);
    detail::Log("queued", "native-table-pending");
    return true;
}

inline void Tick() noexcept {
    using namespace detail;
    if (ticking || !pending) return;
    ticking = true;
    struct Reset { ~Reset() { ticking = false; } } reset;
    auto item = pending;
    try {
        auto* controller = item->key.controller.Object();
        auto* stateObject = item->key.state.Object();
        auto* transport = item->key.transport.Object();
        if (!controller || !controller->IsA(APBPlayerController::StaticClass()) || !stateObject ||
            !stateObject->IsA(APBPlayerState::StaticClass())) { Log("cancelled", "owner-ended"); Finish(item); return; }
        auto* pc = static_cast<APBPlayerController*>(controller);
        auto* state = static_cast<APBPlayerState*>(stateObject);
        auto* login = reconnect::connections.VerifiedEntry(item->member.connection);
        const bool connected = login && !login->menuHandled && pc->HasAuthority() && !pc->IsActorBeingDestroyed() &&
            pc->PlayerState == state && pc->NetConnection == transport && transport &&
            pc->NetConnection->PlayerController == pc && state->GetDefaultIDStr().ToString() == item->member.steam;
        if (!connected) { Log("cancelled", "connection-changed"); Finish(item); return; }
        if (state->HasSelectedRole()) { Log("cancelled", "player-selected-role"); Finish(item); return; }
        if (Clock::now() >= item->expires) { Log("cancelled", "readiness-deadline"); Finish(item); return; }
        if (!SupportedImage()) { Log("cancelled", "unsupported-game-image"); Finish(item); return; }
        if (pc->PBPlayerState != state) { Wait(item, "native-player-state-pending"); return; }
        if (!pc->bHasLoadedArchive) { Wait(item, "archive-loading"); return; }
        if (!pc->bHasLoadedCharacterAvailableInventorys) { Wait(item, "inventory-loading"); return; }
        const auto nativeRoles = native::ReadRoles(reinterpret_cast<std::uintptr_t>(state), Read);
        if (!nativeRoles) { Wait(item, "native-role-table-empty-or-invalid"); return; }
        if (!gate.Observe(item->key, *nativeRoles, static_cast<int>(nativeRoles->size()))) {
            Wait(item, "native-role-keys-invalid"); return;
        }
        const auto result = gate.Tick(item->key, {true, true, true, false}, [&](std::size_t index) {
            if (item->key.controller.Object() != pc || item->key.state.Object() != state ||
                item->key.transport.Object() != transport) return false;
            using Slot = EPBCharacterSlotType;
            constexpr std::array slots{Slot::FirstWeapon, Slot::SecondWeapon, Slot::LeftPod, Slot::RightPod, Slot::MeleeWeapon, Slot::Mobility};
            FPBInventoryNetworkConfig inventory;
            for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                inventory.CharacterSlots.Add(slots[slot]);
                inventory.InventoryItems.Add(Name(item->rows[index].items[slot]));
            }
            const auto role = Name(item->rows[index].id);
            if (pending != item || item->key.controller.Object() != pc || item->key.state.Object() != state ||
                item->key.transport.Object() != transport || pc->PlayerState != state || pc->PBPlayerState != state || pc->NetConnection != transport ||
                pc->NetConnection->PlayerController != pc || state->HasSelectedRole() || !pc->bHasLoadedArchive ||
                !pc->bHasLoadedCharacterAvailableInventorys) return false;
            // 每次原生调用之前重新只读检查；上一角色的处理可重入引擎并改变当前表。
            const auto current = native::ReadRoles(reinterpret_cast<std::uintptr_t>(state), Read);
            if (!current || std::find(current->begin(), current->end(), RoleKey(role)) == current->end()) return false;
            pc->ServerPreOrderInventory(role, inventory);
            return true;
        });
        if (result == Progress::Complete) {
            Finish(item);
            Log("submitted", "six-native-roles");
            std::puts("[BRT-INVENTORY] owner-preorder-submitted roles=6 slots=36");
            std::fflush(stdout);
            item->room.MenuSubmitted(item->member);
        } else if (result == Progress::Cancelled) { Log("cancelled", "queue-replaced"); Finish(item); }
        else Wait(item, "native-role-missing");
    } catch (const std::exception&) { Log("cancelled", "local-error"); Finish(item); }
}
}

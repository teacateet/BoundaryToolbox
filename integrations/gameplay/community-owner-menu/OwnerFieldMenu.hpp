#pragma once
#include "PreorderReady.hpp"
#include "SDK/ProjectBoundary_parameters.hpp"
#include "FieldMenuRows.hpp"

namespace brt::field_menu {
using Snapshot = preorder::detail::Pending;
inline std::shared_ptr<Snapshot> initial;
inline void Bind() { initial = preorder::detail::pending; }

inline void ControllerEvent(SDK::UObject* object, const std::string& event) noexcept {
    if (!object || preorder::detail::ticking) return;
    const bool edit = event == "Function ProjectBoundary.PBPlayerController.ServerPreOrderInventory";
    if (!edit && event != "Function ProjectBoundary.PBPlayerController.ServerConfirmRoleSelection") return;
    try {
        if (initial && initial->key.controller.Object() == object) initial.reset();
        const auto p = preorder::detail::pending;
        if (!p || p->key.controller.Object() != object) return;
        preorder::detail::Finish(p);
        preorder::detail::Log("cancelled", edit ? "native-player-order" : "native-role-confirmed");
    } catch (const std::exception&) {}
}

using Args = SDK::Params::PBPlayerState_ClientInitFieldMod;
using Output = Packet<Args, SDK::FPBInventoryNetworkConfig>;
inline std::unique_ptr<Output> Prepare(SDK::UObject* object, void* params) noexcept {
    try {
        const auto p = initial;
        if (!object || !params || !p || p->key.state.Object() != object) return {};
        // 只准备这个连接的首次菜单。构建失败或随后发生手选，也不会重播旧快照。
        initial.reset();
        struct StopOrders {
            const std::shared_ptr<Snapshot>& p;
            ~StopOrders() {
                if (preorder::detail::pending == p) {
                    preorder::detail::Finish(p);
                    preorder::detail::Log("cancelled", "hand-menu-opened");
                }
            }
        } stop{p};
        if (preorder::detail::Clock::now() >= p->expires) {
            preorder::detail::Log("field-menu-stock", "snapshot-expired"); return {};
        }
        auto* state = static_cast<SDK::APBPlayerState*>(object);
        auto* c = p->key.controller.Object();
        if (!c || !c->IsA(SDK::APBPlayerController::StaticClass())) return {};
        auto* pc = static_cast<SDK::APBPlayerController*>(c);
        const auto key = preorder::detail::KeyFor(pc, state);
        if (!key || !(*key == p->key) || !reconnect::connections.VerifiedEntry(p->member.connection) ||
            state->HasSelectedRole() || state->GetDefaultIDStr().ToString() != p->member.steam) {
            preorder::detail::Log("field-menu-stock", "owner-changed-or-selected"); return {};
        }
        using S = SDK::EPBCharacterSlotType;
        constexpr std::array slots{S::FirstWeapon,S::SecondWeapon,S::LeftPod,S::RightPod,S::MeleeWeapon,S::Mobility};
        auto out = Build(*static_cast<const Args*>(params), p->rows, preorder::detail::Name, slots);
        if (!out) preorder::detail::Log("field-menu-stock", "invalid-or-no-eligible-roles");
        // 只在初始回包交给玩家之前尝试原有检查；之后不再从 Tick 向手选菜单补写旧配置。
        else if (out->restored == 6 && preorder::detail::pending == p) {
            preorder::Tick();
            if (p->key.controller.Object() != pc || p->key.state.Object() != state ||
                pc->PlayerState != state || pc->NetConnection != p->key.transport.Object() ||
                pc->NetConnection->PlayerController != pc || state->HasSelectedRole()) return {};
        }
        return out;
    } catch (const std::exception&) { preorder::detail::Log("field-menu-stock", "local-error"); return {}; }
}
inline void Forwarded(int restored) {
    char reason[80]{};
    std::snprintf(reason, sizeof(reason), "roles-%d-slots-%d", restored, 6 * restored);
    preorder::detail::Log("field-menu-forwarded", reason);
}
}

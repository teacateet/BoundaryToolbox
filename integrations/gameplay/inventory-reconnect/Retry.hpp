#pragma once
#include "OwnerLogin.hpp"
#include "Reconnect.hpp"

namespace brt::reconnect {
struct PendingLogin {
    Identity mode, controller, transport;
    std::string room, owner;
};
inline std::optional<PendingLogin> pending;

inline void WaitForDisconnect(AGameMode& mode, APBPlayerController& pc, const std::string& room, const std::string& owner) {
    auto* wire=pc.NetConnection;
    if (!UKismetSystemLibrary::IsValid(wire) || wire->PlayerController!=&pc) return;
    const Identity controller(&pc), transport(wire);
    if (!connections.IsDifferentVerifiedSession(room,owner,controller,transport)) return;
    if (pending && pending->controller==controller && pending->transport==transport) return;
    pending.emplace(PendingLogin{Identity(&mode),controller,transport,room,owner});
}

inline void Tick() noexcept {
    if (!pending) return;
    try {
        auto* pc=static_cast<APBPlayerController*>(pending->controller.Object());
        auto* mode=static_cast<AGameMode*>(pending->mode.Object());
        if (!pc || !mode || mode->IsActorBeingDestroyed() || !pc->HasAuthority() || pc->IsActorBeingDestroyed() ||
            !pc->NetConnection || pc->NetConnection!=pending->transport.Object() || pc->NetConnection->PlayerController!=pc || !pc->PlayerState ||
            !pc->PlayerState->IsA(APBPlayerState::StaticClass())) { pending.reset(); return; }
        if (!connections.IsDifferentVerifiedSession(pending->room,pending->owner,pending->controller,pending->transport)) {
            pending.reset();
            return;
        }
        if (!connections.PreviousIsDetached(Disconnected)) return;
        pending.reset();
        BrtPreorderOwnerInventory(mode,pc);
    } catch(const std::exception&) { pending.reset(); }
}
}

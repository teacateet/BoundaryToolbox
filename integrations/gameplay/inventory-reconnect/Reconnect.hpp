#pragma once
#include "ConnectionBook.hpp"
#include "LoginRows.hpp"
#include "SDK/ProjectBoundary_classes.hpp"

namespace brt::reconnect {
using namespace SDK;
struct Identity {
    TSoftObjectPtr<UObject> key;
    explicit Identity(UObject* o) : key(UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(o)) {}
    bool operator==(const Identity& value) const {
        return key.WeakPtr.ObjectIndex == value.key.WeakPtr.ObjectIndex &&
               key.WeakPtr.ObjectSerialNumber == value.key.WeakPtr.ObjectSerialNumber;
    }
    bool Valid() const { return key.WeakPtr.ObjectSerialNumber > 0; }
    UObject* Object() const {
        if (!Valid()) return nullptr;
        auto* object = UKismetSystemLibrary::Conv_SoftObjectReferenceToObject(key);
        return UKismetSystemLibrary::IsValid(object) && *this == Identity(object) ? object : nullptr;
    }
};
inline ConnectionBook<Identity> connections;

inline bool Disconnected(const Identity& controller, const Identity& transport) {
    auto* old = static_cast<APBPlayerController*>(controller.Object());
    auto* net = static_cast<UNetConnection*>(transport.Object());
    return !old || !net || old->IsActorBeingDestroyed() || old->NetConnection != net || net->PlayerController != old;
}
inline std::string NewRequest() {
    GUID id{};
    wchar_t text[40]{};
    if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id,text,40) != 39) return {};
    std::string ascii;
    for (unsigned i = 1; i < 37; ++i) ascii.push_back(static_cast<char>(text[i]));
    return ascii;
}

struct Menu {
    spawn::Member member;
    std::vector<spawn::SavedMenuRole> rows;
};
inline std::optional<Menu> ReadMenu(const spawn::RoomHttp& room, const std::string& steam, APBPlayerController& pc) {
    auto* net = pc.NetConnection;
    if (!login::IsSteamAccount(steam) || pc.IsActorBeingDestroyed() || !UKismetSystemLibrary::IsValid(net) ||
        net->PlayerController != &pc) return {};
    const Identity controller(&pc), transport(net);
    if (!controller.Valid() || !transport.Valid()) return {};
    auto* user = connections.Start(room.RoomId(),steam,controller,transport,Disconnected,NewRequest);
    if (!user) return {};
    spawn::Member member{room.RoomId(),user->request,steam};
    const auto predecessor = user->previous;
    std::string used = predecessor, doc;
    try { doc = room.ReconnectMenu(member,predecessor); }
    catch (const spawn::RoomHttpStatus& error) {
        // 上一次 PostLogin 如在服务器收到请求之前断线，使用最近已回读的前任重试。
        if (error.status != 409 || user->verified || predecessor == connections.Confirmed()) throw;
        used = connections.Confirmed();
        doc = room.ReconnectMenu(member,used);
    }
    auto rows = spawn::ReadOwnerMenu(member,doc);
    if (!rows || !login::ValidRows(*rows) || pc.NetConnection != net || net->PlayerController != &pc) return {};
    connections.Verified(member.connection,used);
    return Menu{std::move(member),std::move(*rows)};
}
}

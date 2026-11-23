#pragma once
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

struct Calls {
    int birth{}, orders{}, acks{};
    bool menuHandled{}, birthReady{}, selectOnRead{}, failWatch{};
    bool Finished() const { return menuHandled && birthReady; }
};
inline Calls calls;
namespace SDK {
struct FString {
    FString(const wchar_t*) {}
    std::string ToString() const { return "owner"; }
};
struct FName {};
struct UKismetStringLibrary {
    static FName Conv_StringToName(FString) { return {}; }
};
template<class T> struct List {
    std::vector<T> values;
    void Add(T value) { values.push_back(value); }
};
enum class EPBCharacterSlotType { FirstWeapon, SecondWeapon, LeftPod, RightPod, MeleeWeapon, Mobility };
struct FPBInventoryNetworkConfig { List<EPBCharacterSlotType> CharacterSlots; List<FName> InventoryItems; };
struct APBPlayerState {
    bool selected{};
    static int StaticClass() { return 1; }
    bool IsA(int) const { return true; }
    bool HasSelectedRole() const { return selected; }
    FString GetDefaultIDStr() const { return L"owner"; }
};
struct AGameMode { bool HasAuthority() const { return true; } };
struct APBPlayerController;
struct UNetConnection { APBPlayerController* PlayerController; };
struct APBPlayerController {
    APBPlayerState* PlayerState{};
    UNetConnection* NetConnection{};
    bool HasAuthority() const { return true; }
    void ServerPreOrderInventory(FName, const FPBInventoryNetworkConfig& inventory) {
        if (inventory.CharacterSlots.values.size() == 6 && inventory.InventoryItems.values.size() == 6) ++calls.orders;
    }
};
}
namespace brt::spawn {
struct Member { std::string connection{"session"}; };
struct SavedMenuRole { std::string id; std::array<std::string,6> items; };
struct RoomHttp {
    static std::optional<RoomHttp> FromLocalServer() { return RoomHttp{}; }
    std::string RoomId() const { return "room"; }
    void MenuSubmitted(const Member&) const { ++calls.acks; }
};
}
namespace brt::grapple {
inline void BindVerified(SDK::AGameMode*, SDK::APBPlayerController*, const std::string&) {}
}
namespace brt::reconnect {
struct Menu { spawn::Member member; std::array<spawn::SavedMenuRole,6> rows; };
inline std::optional<Menu> ReadMenu(const spawn::RoomHttp&, const std::string&, SDK::APBPlayerController& pc) {
    if (calls.Finished()) return {};
    if (calls.selectOnRead) pc.PlayerState->selected = true;
    return Menu{};
}
struct Connection {
    Calls* VerifiedEntry(const std::string&) { return &calls; }
};
inline Connection connections;
inline void WaitForDisconnect(SDK::AGameMode&, SDK::APBPlayerController&, const std::string&, const std::string&) {}
inline void Tick() {}
}
inline bool BrtWatchOwnerBirth(SDK::APBPlayerController*, const brt::spawn::RoomHttp&,
    const std::string&, const std::array<brt::spawn::SavedMenuRole,6>&) {
    ++calls.birth;
    return !calls.failWatch;
}
void BrtPreorderOwnerInventory(SDK::AGameMode*, SDK::APBPlayerController*) noexcept;

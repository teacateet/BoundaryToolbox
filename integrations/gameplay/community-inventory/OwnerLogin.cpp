#include "OwnerLogin.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include "LoginRows.hpp"
#include <cstdio>

namespace {
using namespace SDK;

FName ItemName(const std::string& ascii) {
    const std::wstring wide(ascii.begin(), ascii.end());
    return UKismetStringLibrary::Conv_StringToName(FString(wide.c_str()));
}
}

void BrtPreorderOwnerInventory(SDK::AGameMode* gameMode, SDK::APBPlayerController* player) noexcept {
    try {
        const auto room = brt::spawn::RoomHttp::FromLocalServer();
        if (!room || !gameMode || !player || !gameMode->HasAuthority() || !player->HasAuthority()) return;
        auto* state = player->PlayerState;
        if (!state || !state->IsA(SDK::APBPlayerState::StaticClass())) return;
        auto* owner = static_cast<SDK::APBPlayerState*>(state);
        if (owner->HasSelectedRole()) return;
        const auto steam = owner->GetDefaultIDStr().ToString();
        const auto rows = brt::login::ReadAtLogin(*room, steam);
        if (!rows || owner->HasSelectedRole()) return;

        using Slot = SDK::EPBCharacterSlotType;
        constexpr std::array slots{Slot::FirstWeapon, Slot::SecondWeapon, Slot::LeftPod,
                                   Slot::RightPod, Slot::MeleeWeapon, Slot::Mobility};
        // 先构造完整的六名角色。游戏决定是否接受每一组预订，保留原来的配额及菜单响应。
        struct Order { SDK::FName role; SDK::FPBInventoryNetworkConfig inventory; };
        std::array<Order, 6> orders{};
        for (std::size_t i = 0; i < rows->size(); ++i) {
            orders[i].role = ItemName((*rows)[i].id);
            for (std::size_t j = 0; j < slots.size(); ++j) {
                orders[i].inventory.CharacterSlots.Add(slots[j]);
                orders[i].inventory.InventoryItems.Add(ItemName((*rows)[i].items[j]));
            }
        }
        for (const auto& order : orders) player->ServerPreOrderInventory(order.role, order.inventory);
        std::puts("[BRT-INVENTORY] owner-preorder-submitted roles=6 slots=36");
        std::fflush(stdout);
    } catch (const std::exception&) {
        // 本机档案未就绪、不是房主或资源不可用时，让正常的游戏登录流程继续。
    }
}

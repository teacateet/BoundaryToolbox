#pragma once
#include "SpawnConsumer.hpp"
#include <array>
#include <optional>
#include <set>
#include <vector>

namespace brt::spawn {

// 在房主首次初始化此连接的选角页时，把军械库的六个槽带到原有角色和配额里。
struct SavedMenuRole {
    Name id;
    std::array<Name, 6> items;
};

inline std::optional<std::vector<SavedMenuRole>> ReadOwnerMenu(const Member& owner, std::string_view reply) {
    if (reply.empty() || reply.size() > 16384) return {};
    const Json doc = Json::parse(reply.begin(), reply.end(), nullptr, false);
    if (!doc.is_object()) return {};
    std::vector<SavedMenuRole> saved;
    std::set<Name> seen;
    try {
        if (doc.at("version") != 1 || doc.at("connectionId") != owner.connection ||
            doc.at("roomId") != owner.room || doc.at("authenticatedSteamId") != owner.steam)
            return {};
        const auto& roles = doc.at("roles");
        if (!roles.is_array() || roles.size() > 6) return {};
        for (const auto& row : roles) {
            SavedMenuRole role{ReadName(row, "CharacterID"), {}};
            if (!seen.insert(role.id).second) return {};
            const auto& items = row.at("InventoryItems");
            if (!items.is_array() || items.size() != role.items.size()) return {};
            for (size_t slot = 0; slot < role.items.size(); ++slot) {
                role.items[slot] = items.at(slot).get<Name>();
                const auto& id = role.items[slot];
                if (id.empty() || id.size() > 256 || id.find_first_of("\r\n\t") != Name::npos) return {};
            }
            saved.push_back(std::move(role));
        }
    } catch (const std::exception&) { return {}; }
    return saved;
}

// 代替宿主原来的 ClientInitFieldMod 调用。初始化完成后不再次覆盖正在编辑的槽。
// 只遍历房间准许的角色，以原本配额初始化本玩家的 PlayerState。
template<class PlayerState, class Menu, class RoleIds, class Quotas, class Intern>
bool InitOwnerFieldMod(PlayerState& state, const Member& owner, std::string_view reply,
                       const Menu& original, const RoleIds& allowed, const Quotas& quotas, Intern&& name) {
    const auto saved = ReadOwnerMenu(owner, reply);
    if (!saved || allowed.Num() != quotas.Num() || original.RoleArray.Num() != original.RoleInventoryNetworkConfigArray.Num())
        return false;
    using Inventory = std::remove_cv_t<std::remove_reference_t<decltype(original.RoleInventoryNetworkConfigArray[0])>>;
    using Slot = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Inventory&>().CharacterSlots[0])>>;
    Menu updated{};
    // 从值构造新配置，原有数组始终归游戏所有。缺席的角色保留服务器自己的装备。
    auto fill = [&](const auto& role, const Inventory* stock) {
        const auto found = std::find_if(saved->begin(), saved->end(), [&](const auto& item) { return name(item.id) == role; });
        bool available = false;
        for (int i = 0; i < allowed.Num(); ++i) if (allowed[i] == role) { available = true; break; }
        if (!available || found == saved->end()) {
            if (stock) {
                updated.RoleArray.Add(role);
                updated.RoleInventoryNetworkConfigArray.Add(*stock);
            }
            return;
        }
        Inventory inventory{};
        for (int i = 0; i < 6; ++i) {
            inventory.CharacterSlots.Add(static_cast<Slot>(i + 1));
            inventory.InventoryItems.Add(name(found->items[i]));
        }
        updated.RoleArray.Add(role);
        updated.RoleInventoryNetworkConfigArray.Add(inventory);
    };
    for (int i = 0; i < original.RoleArray.Num(); ++i) fill(original.RoleArray[i], &original.RoleInventoryNetworkConfigArray[i]);
    for (int i = 0; i < allowed.Num(); ++i) {
        bool exists = false;
        for (int j = 0; j < original.RoleArray.Num(); ++j) if (original.RoleArray[j] == allowed[i]) { exists = true; break; }
        if (!exists) fill(allowed[i], nullptr);
    }
    state.ClientInitFieldMod(updated, allowed, quotas);
    return true;
}

} // namespace brt::spawn

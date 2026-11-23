#pragma once
#include <array>
#include <string>

namespace brt::archive {
struct PartsReadback {
    int actual{}, desired{};
    std::string difference;
};

template <typename Config, typename Name>
PartsReadback CompareFittedParts(const Config& actual, const Config& desired, const Name& none) {
    PartsReadback result;
    using Index = std::array<int, 13>;
    Index got, wanted;
    auto index = [&](const Config& config, Index& slots, int& count, const std::string& side) {
        slots.fill(-1);
        bool seen[13]{};
        if (config.WeaponPartConfigs.Num() != config.WeaponPartSlotTypeArray.Num())
            return side + "-arrays";
        for (int i = 0; i < config.WeaponPartConfigs.Num(); ++i) {
            auto slot = static_cast<unsigned>(config.WeaponPartSlotTypeArray[i]);
            if (slot == 0 || slot >= slots.size() || seen[slot]) return side + "-slots";
            seen[slot] = true;
            if (config.WeaponPartConfigs[i].WeaponPartID == none) continue;
            slots[slot] = i;
            ++count;
        }
        return std::string{};
    };
    result.difference = index(actual, got, result.actual, "actual");
    if (!result.difference.empty()) return result;
    result.difference = index(desired, wanted, result.desired, "desired");
    if (!result.difference.empty()) return result;
    if (actual.WeaponID != desired.WeaponID) result.difference = "weapon-id";
    else if (actual.WeaponClassID != desired.WeaponClassID) result.difference = "class-id";
    else if (actual.OrnamentID != desired.OrnamentID) result.difference = "ornament";
    for (unsigned slot = 1; result.difference.empty() && slot < got.size(); ++slot) {
        if (got[slot] == -1 && wanted[slot] == -1) continue;
        std::string reason;
        if (got[slot] == -1) reason = "slot-missing";
        else if (wanted[slot] == -1) reason = "slot-extra";
        else {
            const auto& a = actual.WeaponPartConfigs[got[slot]];
            const auto& b = desired.WeaponPartConfigs[wanted[slot]];
            if (a.WeaponPartID != b.WeaponPartID) reason = "part";
            else if (a.WeaponPartSkinID != b.WeaponPartSkinID) reason = "skin";
            else if (a.WeaponPartSkinPaintingID != b.WeaponPartSkinPaintingID) reason = "paint";
            else if (a.WeaponPartSpecialSkinID != b.WeaponPartSpecialSkinID) reason = "special";
        }
        if (!reason.empty()) result.difference = reason + "-" + std::to_string(slot);
    }
    if (result.difference.empty()) result.difference = "none";
    return result;
}
}

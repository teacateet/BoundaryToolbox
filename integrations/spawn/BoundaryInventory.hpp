#pragma once

#include "SpawnConsumer.hpp"
#include <type_traits>

namespace brt::spawn {

template<class Array>
using Item = std::remove_cv_t<std::remove_reference_t<decltype(std::declval<Array&>()[0])>>;

// base 是按本次已批准角色及所选枪建立的待生成值。填充完整后才交给宿主创建实体。
template<class WeaponConfig, class Intern>
bool FillWeapon(const Weapon& in, WeaponConfig& base, Intern&& name, const WeaponConfig* actorBase = nullptr) {
    if (!in.Present()) {
        base = WeaponConfig{};
        base.WeaponID = name(None);
        base.WeaponClassID = name(None);
        base.OrnamentID = name(None);
        return true;
    }
    if (base.WeaponID != name(in.id) || (in.classId && base.WeaponClassID != name(*in.classId)))
        return false;
    using Slot = Item<decltype(base.WeaponPartSlotTypeArray)>;
    using Config = Item<decltype(base.WeaponPartConfigs)>;
    const WeaponConfig& source = actorBase ? *actorBase : base;
    if (source.WeaponPartSlotTypeArray.Num() != source.WeaponPartConfigs.Num()) return false;
    std::optional<Config> receiver;
    std::array<bool, 13> seen{};
    for (int index = 0; index < source.WeaponPartSlotTypeArray.Num(); ++index) {
        const auto slot = static_cast<unsigned>(source.WeaponPartSlotTypeArray[index]);
        if (slot < 1 || slot > 12 || slot == 11 || seen[slot]) return false;
        seen[slot] = true;
        if (slot == 12) receiver = source.WeaponPartConfigs[index];
    }
    // 档案的十个改装槽不包含机匣。只有原作表能把已选套装与机匣部件和涂装对应起来。
    if (in.receiver) {
        const auto expected = name(in.receiver->part);
        if (!receiver) receiver.emplace();
        if (receiver->WeaponPartID != name(None) && receiver->WeaponPartID != expected && seen[12])
            return false;
        receiver->WeaponPartID = expected;
        receiver->WeaponPartSkinID = name(in.receiver->skin);
        receiver->WeaponPartSkinPaintingID = name(in.receiver->paint);
        if (!seen[12]) receiver->WeaponPartSpecialSkinID = name(None);
    }
    base.OrnamentID = name(in.ornament);
    base.WeaponPartSlotTypeArray = {};
    base.WeaponPartConfigs = {};
    for (std::size_t slot = 0; slot < in.parts.size(); ++slot) {
        const auto& part = in.parts[slot];
        Config config{};
        config.WeaponPartID = name(part.id);
        config.WeaponPartSkinID = name(part.skin);
        config.WeaponPartSpecialSkinID = name(part.special);
        config.WeaponPartSkinPaintingID = name(part.paint);
        base.WeaponPartSlotTypeArray.Add(static_cast<Slot>(slot + 1));
        base.WeaponPartConfigs.Add(config);
    }
    if (receiver) {
        base.WeaponPartSlotTypeArray.Add(static_cast<Slot>(12));
        base.WeaponPartConfigs.Add(*receiver);
    }
    return true;
}

template<class Config, class Intern>
void FillItem(Config& current, const Name& selected, Intern&& name) {
    auto id = name(selected);
    if (current.ID != id || selected == None) current.SkinID = name(None);
    current.ID = id;
}

// expected 必须是宿主批准的当前角色的默认外观。通过默认 ID 在已存在的原生数组中辨认类别，
// 各角色/版本即使用不同枚举顺序也不会互相覆盖；无法辨认时保留原状并报告失败。
template<class Network, class Defaults, class Intern>
bool FillCharacter(const CharacterAppearance& look, Network& current, const Network& stock, const Defaults& expected, Intern&& name) {
    const auto none = name(None);
    struct Entry { const std::optional<Name>* change; decltype(none) defaultId; };
    const Entry entries[]{{&look.suit,expected.SpacesuitID},{&look.badge,expected.ArmBadgeID},{&look.head,expected.HeadAccessoryID}};
    using Tag = Item<decltype(stock.SkinClassArray)>;
    using NativeName = Item<decltype(current.SkinIDArray)>;
    if (stock.SkinClassArray.Num() != stock.SkinIDArray.Num() || current.SkinClassArray.Num() != current.SkinIDArray.Num()) return false;
    // EPBSkinClass 的实际有效值为 1 至 5，0 为 None，6 为终止哨兵。
    std::array<bool,6> used{}, usedDefaults{}, targets{};
    for (int i=0; i<stock.SkinClassArray.Num(); ++i) {
        const auto tag = static_cast<unsigned>(stock.SkinClassArray[i]);
        if (!tag || tag >= usedDefaults.size() || usedDefaults[tag]) return false;
        usedDefaults[tag] = true;
    }
    for (int i=0; i<current.SkinClassArray.Num(); ++i) {
        const auto tag = static_cast<unsigned>(current.SkinClassArray[i]);
        if (!tag || tag >= used.size() || used[tag]) return false;
        used[tag] = true;
    }
    std::array<std::optional<NativeName>,6> values;
    for (const auto& entry : entries) {
        if (!*entry.change) continue;
        if (entry.defaultId == none) return false;
        std::optional<unsigned> slot;
        for (int i=0; i<stock.SkinIDArray.Num(); ++i) {
            if (stock.SkinIDArray[i] != entry.defaultId) continue;
            if (slot) return false;
            slot = static_cast<unsigned>(stock.SkinClassArray[i]);
        }
        if (!slot || targets[*slot]) return false;
        targets[*slot] = true;
        values[*slot] = name(**entry.change);
    }
    // 不就地改 TArray，避免污染 UE 默认对象或宿主传入的配置。
    decltype(current.SkinClassArray) classes{};
    decltype(current.SkinIDArray) ids{};
    for (int i=0; i<current.SkinClassArray.Num(); ++i) {
        const auto tag = static_cast<unsigned>(current.SkinClassArray[i]);
        classes.Add(current.SkinClassArray[i]);
        ids.Add(values[tag] ? *values[tag] : current.SkinIDArray[i]);
    }
    for (unsigned tag=0; tag<targets.size(); ++tag) if (targets[tag] && !used[tag]) {
        classes.Add(static_cast<Tag>(tag));
        ids.Add(*values[tag]);
    }
    current.SkinClassArray = std::move(classes);
    current.SkinIDArray = std::move(ids);
    if (look.painting) current.SkinPaintingID = name(*look.painting);
    return true;
}

template<class RoleConfig, class Intern, class Finish>
bool BuildInventoryCore(const Loadout& in, const RoleConfig& approvedBase, Intern&& name, Finish&& finish) {
    if (approvedBase.CharacterID != name(in.role)) return false;
    RoleConfig pending = approvedBase;
    if (!FillWeapon(in.primary, pending.FirstWeaponPartData, name) ||
        !FillWeapon(in.secondary, pending.SecondWeaponPartData, name)) return false;
    FillItem(pending.LeftLauncherData, in.left, name);
    FillItem(pending.RightLauncherData, in.right, name);
    FillItem(pending.MeleeWeaponData, in.melee, name);
    pending.MobilityModuleData.MobilityModuleID = name(in.mobility);
    auto& inventory = pending.InventoryData;
    inventory.CharacterSlots = {};
    inventory.InventoryItems = {};
    using Slot = Item<decltype(inventory.CharacterSlots)>;
    const std::array<const Name*, 6> ids{{ &in.primary.id, &in.secondary.id, &in.left, &in.right, &in.melee, &in.mobility }};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        inventory.CharacterSlots.Add(static_cast<Slot>(i + 1));
        inventory.InventoryItems.Add(name(*ids[i]));
    }
    // 整枪涂装和部件的额外挂饰随 in 传递。宿主先解析外观，再在同一次生成中提交角色与两个武器实参。
    return finish(pending, in);
}

// 老宿主若不具备角色外观上下文，不得把含有外观的出生报告成已经完整提交。
template<class RoleConfig, class Intern, class Finish>
bool BuildInventory(const Loadout& in, const RoleConfig& approved, Intern&& name, Finish&& finish) {
    return !in.character && BuildInventoryCore(in, approved, name, finish);
}

// 同一干员的默认网络配置及具名字段都来自宿主，不修改共享的角色默认对象。
template<class RoleConfig, class Defaults, class Intern, class Finish>
bool BuildDressedInventory(const Loadout& in, const RoleConfig& approved, const RoleConfig& stock,
    const Defaults& defaults, Intern&& name, Finish&& finish) {
    if (stock.CharacterID != approved.CharacterID || stock.CharacterID != name(in.role)) return false;
    return BuildInventoryCore(in, approved, name, [&](auto& pending, const Loadout& plan) {
        if (in.character && !FillCharacter(*in.character, pending.CharacterData, stock.CharacterData, defaults, name)) return false;
        return finish(pending, plan);
    });
}

// 可在宿主创建了正确枪型、尚未将其交给 Pawn 的正常库存初始化分支调用。
// 此处的两个成员分别对应 APBWeapon 和 FPBWeaponNetworkConfig。
template<class WeaponActor, class WeaponConfig>
void InitSpawnedWeapon(WeaponActor& actor, const WeaponConfig& config) {
    actor.InitWeapon(config, false);
}

// 宿主的库存创建流程持有这一批尚未发布的新实例，失败时由原流程释放整批。
// BeginBatch 负责按已批准的 role 值创建两个枪槽、左右挂载、移动和近战。Publish 必须一次性完成。
template<class Assemble, class BeginBatch>
Result AcceptWithInventoryBuilder(Consumer& member, std::uint64_t pawnGeneration, std::string_view reply,
                                 Assemble&& assemble, BeginBatch&& beginBatch) {
    return member.Accept(pawnGeneration, reply, [&](const Loadout& plan) {
        return assemble(plan, [&](const auto& prepared, const Loadout&) {
            auto batch = beginBatch(prepared, pawnGeneration);
            if (!batch) return false;
            auto* first = batch->PrimaryWeapon();
            auto* second = batch->SecondaryWeapon();
            if ((first != nullptr) != plan.primary.Present() || (second != nullptr) != plan.secondary.Present() ||
                (first && first == second)) return false;
            // 在写入枪实例之前解析整枪与部件外观。现场没有支持外观的接口时留待重试。
            if ((first && !batch->StageAppearance(*first, plan.primary)) ||
                (second && !batch->StageAppearance(*second, plan.secondary))) return false;
            if (first) InitSpawnedWeapon(*first, prepared.FirstWeaponPartData);
            if (second) InitSpawnedWeapon(*second, prepared.SecondWeaponPartData);
            if (!member.Targets(pawnGeneration)) return false;
            batch->Publish();
            return true;
        });
    });
}

template<class RoleConfig, class Intern, class BeginBatch>
Result AcceptAtInventorySpawn(Consumer& member, std::uint64_t generation, std::string_view reply,
                              const RoleConfig& approved, Intern&& name, BeginBatch&& beginBatch) {
    return AcceptWithInventoryBuilder(member, generation, reply, [&](const Loadout& plan, auto&& finish) {
        return BuildInventory(plan, approved, name, finish);
    }, beginBatch);
}

template<class RoleConfig, class Defaults, class Intern, class BeginBatch>
Result AcceptAtInventorySpawn(Consumer& member, std::uint64_t generation, std::string_view reply,
                              const RoleConfig& approved, const RoleConfig& stock, const Defaults& defaults,
                              Intern&& name, BeginBatch&& beginBatch) {
    return AcceptWithInventoryBuilder(member, generation, reply, [&](const Loadout& plan, auto&& finish) {
        return BuildDressedInventory(plan, approved, stock, defaults, name, finish);
    }, beginBatch);
}

} // namespace brt::spawn

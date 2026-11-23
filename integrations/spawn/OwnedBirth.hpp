#pragma once

#include "BoundaryInventory.hpp"

namespace brt::spawn {

// 该结果只描述本次普通武器初始化调用。Prepare 单独验证整份档案。
enum class BirthInit { Initialized, AlreadyInitialized, Rejected };

template<class Controller, class Pawn, class Actor>
class OwnedBirth {
    using NativeName = decltype(std::declval<Actor&>().GetWeaponId());
    using Config = decltype(std::declval<Actor&>().GetPBWeaponPartSaveConfig());
    struct Identity {
        NativeName item, weaponClass;
        bool operator==(const Identity& other) const {
            return item == other.item && weaponClass == other.weaponClass;
        }
    };
    Consumer receiver_;
    Controller& controller_;
    Pawn* pawn_{};
    std::uint64_t life_{};
    std::array<Identity, 2> approved_{};
    std::optional<Loadout> plan_;
    // 指针只参与身份比较。只解引用游戏线程当前调用提供的存活对象。
    std::array<const Actor*, 2> initialized_{};

    template<class Intern>
    static bool MatchesSelected(const Name& role, const Name& selected, const Identity& ref, Intern& name) {
        const auto none = name(None);
        if (selected == None) return ref.item == none && ref.weaponClass == none;
        const auto prefix = role + "_";
        if (selected.size() <= prefix.size() || selected.compare(0, prefix.size(), prefix) != 0 ||
            ref.weaponClass == none) return false;
        // 角色定义表是 PROBE_GSW-DMR，枪定义表是 GSW-DMR。实际 ID 始终取宿主批准值。
        return ref.item == name(selected) || ref.item == name(selected.substr(prefix.size()));
    }

    template<class Intern>
    static bool PrepareWeapon(Actor& actor, const Weapon& wire, const Identity& ref, Intern& name, Config& out) {
        if (actor.GetWeaponId() != ref.item || actor.GetWeaponClassID() != ref.weaponClass ||
            (wire.classId && name(*wire.classId) != ref.weaponClass)) return false;
        const auto saved = actor.GetPBWeaponPartSaveConfig();
        const auto none = name(None);
        if ((saved.WeaponID != none && saved.WeaponID != ref.item) ||
            (saved.WeaponClassID != none && saved.WeaponClassID != ref.weaponClass)) return false;
        out.WeaponID = name(wire.id);
        out.WeaponClassID = ref.weaponClass;
        if (!FillWeapon(wire, out, name, &saved)) return false;
        // 使用新生实体持有的原生 ID；十个配件和独立机匣涂装已在同一份配置中就绪。
        out.WeaponID = ref.item;
        return true;
    }

public:
    OwnedBirth(Member authenticated, Controller& owner)
        : receiver_(std::move(authenticated)), controller_(owner) {}
    OwnedBirth(const OwnedBirth&) = delete;
    OwnedBirth& operator=(const OwnedBirth&) = delete;

    // 在同一已认证连接的游戏线程上记录本次出生的角色、原生基础配置和已占有的 Pawn。
    template<class RoleConfig, class Intern>
    bool Begin(std::uint64_t life, std::uint64_t generation, const Selection& selected,
               Pawn& pawn, const RoleConfig& native, Intern&& name) {
        if (controller_.K2_GetPawn() != &pawn || native.CharacterID != name(selected.role)) return false;
        std::array<Identity, 2> refs{{
            {native.FirstWeaponPartData.WeaponID, native.FirstWeaponPartData.WeaponClassID},
            {native.SecondWeaponPartData.WeaponID, native.SecondWeaponPartData.WeaponClassID}
        }};
        if (!MatchesSelected(selected.role, selected.primary, refs[0], name) ||
            !MatchesSelected(selected.role, selected.secondary, refs[1], name)) return false;
        if (life == life_ && (pawn_ != &pawn || !(refs == approved_))) return false;
        if (!receiver_.Begin(life, generation, selected)) return false;
        if (life != life_) {
            life_ = life;
            approved_ = std::move(refs);
            pawn_ = &pawn;
            plan_.reset();
            initialized_ = {};
        }
        return true;
    }

    // 房间 Prepare 的响应可在实体创建前准备。重复回包不清除已经初始化的枪槽。
    bool Prepare(std::uint64_t generation, std::string_view reply) {
        if (!pawn_ || controller_.K2_GetPawn() != pawn_) return false;
        const auto result = receiver_.Accept(generation, reply, [&](const Loadout& loaded) {
            plan_ = loaded;
            return true;
        });
        return result == Result::Applied || result == Result::AlreadyApplied;
    }

    // 出生流程先取走这一份角色级配置，用于原有左右挂载、机动、近战和角色外观构建。
    // 武器原生 ID 可能不含干员前缀；用于回包校验的名字与宿主实际创建实物的键分开保存。
    template<class RoleConfig, class Defaults, class Intern, class Finish>
    bool WithInventory(std::uint64_t life, std::uint64_t generation, Pawn& pawn, const RoleConfig& approved,
                       const RoleConfig& stock, const Defaults& defaults, Intern&& name, Finish&& finish) {
        if (!plan_ || life != life_ || !receiver_.Targets(generation) || pawn_ != &pawn || controller_.K2_GetPawn() != &pawn)
            return false;
        const Identity first{approved.FirstWeaponPartData.WeaponID,approved.FirstWeaponPartData.WeaponClassID};
        const Identity second{approved.SecondWeaponPartData.WeaponID,approved.SecondWeaponPartData.WeaponClassID};
        if (!(first==approved_[0]) || !(second==approved_[1])) return false;
        auto normalized = approved;
        normalized.FirstWeaponPartData.WeaponID = name(plan_->primary.id);
        normalized.SecondWeaponPartData.WeaponID = name(plan_->secondary.id);
        return BuildDressedInventory(*plan_,normalized,stock,defaults,name,[&](auto& prepared,const Loadout&) {
            prepared.FirstWeaponPartData.WeaponID = first.item;
            prepared.SecondWeaponPartData.WeaponID = second.item;
            return finish(prepared);
        });
    }

    // 两把枪可先后创建。传入宿主当前主副枪槽的真实引用和此刻要初始化的那一把。
    // 同步调用，不查找全局实体，不持有跨帧待执行的武器操作。
    template<class Intern>
    BirthInit InitCurrent(std::uint64_t life, std::uint64_t generation, Pawn& pawn, Actor* first, Actor* second,
                          Actor& newborn, Intern&& name) {
        if (!plan_ || life != life_ || !receiver_.Targets(generation) || pawn_ != &pawn || controller_.K2_GetPawn() != &pawn ||
            newborn.GetPawnOwner() != &pawn || first == second) return BirthInit::Rejected;
        int slot;
        if (first == &newborn) slot = 0;
        else if (second == &newborn) slot = 1;
        else return BirthInit::Rejected;
        const auto& wire = slot == 0 ? plan_->primary : plan_->secondary;
        const auto& ref = approved_[slot];
        if (!wire.Present() || initialized_[1 - slot] == &newborn ||
            newborn.GetWeaponId() != ref.item || newborn.GetWeaponClassID() != ref.weaponClass)
            return BirthInit::Rejected;
        if (initialized_[slot])
            return initialized_[slot] == &newborn ? BirthInit::AlreadyInitialized : BirthInit::Rejected;
        Config config{};
        if (!PrepareWeapon(newborn, wire, ref, name, config)) return BirthInit::Rejected;
        // 原生 InitWeapon 返回 void。先记下本槽，防止同步回调重入导致同一把枪初始化两次。
        initialized_[slot] = &newborn;
        newborn.InitWeapon(config, false);
        return BirthInit::Initialized;
    }

    void Leave() {
        receiver_.Leave();
        pawn_ = nullptr;
        plan_.reset();
        initialized_ = {};
    }
};

} // namespace brt::spawn

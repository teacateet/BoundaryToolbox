#pragma once
#include <array>

namespace brt::birth {
struct Counts {
    int deaths, bullets, rockets;
};

// 仅在已观察到的未占有或死亡阶段之后消费一次出生。复活同一个旧实体不会通过年龄检查。
class Window {
    bool armed_ = false;
    Counts before_{};
    const void* newborn_{};
    float waitedAt_ = -1.0f;
    static constexpr float MaxAge = 0.25f;
public:
    void Wait(Counts now, float at = -1.0f) noexcept {
        newborn_ = nullptr;
        waitedAt_ = at;
        armed_ = now.deaths >= 0 && now.bullets >= 0 && now.rockets >= 0;
        before_ = now;
    }
    bool Begin() noexcept {
        const bool result = armed_;
        armed_ = false;
        return result;
    }
    // 即便晚了一帧，角色也必须出生在已观察到的空档内。
    bool Start(const void* pawn, float age, float now = -1.0f) noexcept {
        if (!Begin()) return false;
        newborn_ = age == 0.0f || (waitedAt_ >= 0.0f && now >= waitedAt_ &&
            age > 0.0f && age <= MaxAge && age <= now - waitedAt_) ? pawn : nullptr;
        return true;
    }
    bool Pending() const noexcept { return newborn_ != nullptr; }
    void Finish() noexcept { newborn_ = nullptr; }
    bool Owns(const void* pawn, float age) const noexcept {
        return newborn_ && newborn_ == pawn && age >= 0.0f && age <= MaxAge;
    }
    bool Unused(Counts now, float pawnAge, bool running, bool remote, bool acknowledged,
                bool firing, bool targeting) const noexcept {
        return (pawnAge == 0.0f || (Pending() && pawnAge > 0.0f && pawnAge <= MaxAge)) &&
               running && remote && !acknowledged && !firing && !targeting &&
               now.deaths >= before_.deaths && (now.deaths - before_.deaths <= 1) &&
               now.bullets == before_.bullets && now.rockets == before_.rockets;
    }
};

inline bool BornWithPawn(float age, float pawnAge) noexcept {
    return age >= 0.0f && age <= pawnAge;
}
inline bool FreshWeapon(float age, bool owned, bool attached, bool reloading, bool aiming,
                        bool idle, bool equipping, float pawnAge = 0.0f) noexcept {
    return BornWithPawn(age, pawnAge) && owned && !attached && !reloading && !aiming && (idle || equipping);
}

// 一槽未读回时仍处理另一槽，但整套配装必须全部读回才可签收。
template<class Slot, class Check>
bool VerifyOccupied(const std::array<Slot, 2>& slots, Check check) {
    bool any = false, complete = true;
    for (unsigned i = 0; i < slots.size(); ++i) {
        if (!slots[i]) continue;
        any = true;
        if (!check(i, slots[i])) complete = false;
    }
    return any && complete;
}
}

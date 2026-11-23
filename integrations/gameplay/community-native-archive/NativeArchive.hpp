#pragma once
#include "SpawnConsumer.hpp"
#include <cstdint>
#include <cstring>

// Steam Build 14530908 的原生逐角色档案表：APBPlayerState+0x700，TMap<FName, 角色档案>，元素 0x110。
// 原版专服在档案完成回调 0x15BA210 中写入它；出生配置 0x15BD760 从它取枪械配件与角色外观。
// 这里只调用回调自己使用的原生写入函数。调用方必须先确认零售映像版本。
namespace brt::native_archive {
inline constexpr std::uintptr_t MapOffset = 0x700, ElementSize = 0x110;
inline constexpr std::uintptr_t FindRoleRva = 0x15AEB20, WeaponRva = 0x166D980, WeaponSkinRva = 0x16615C0,
    PartRva = 0x16615B0, PartLookRva = 0x16614D0, PartPaintRva = 0x1661560, OrnamentRva = 0x16613F0;
// 角色档案值内的偏移，即元素跳过 8 字节角色键之后。
inline constexpr std::uintptr_t FirstWeapon = 0x00, SecondWeapon = 0x08, Suit = 0x90, Head = 0x98, Badge = 0xA0,
    SuitPaint = 0xA8, Weapons = 0xB0;

// 原生函数按指针读取 8 字节名称或 1 字节枚举；留出余量，不依赖其后的栈内容。
struct Key { std::uint64_t raw{}; std::uint64_t spare[3]{}; };
template<class FName>
Key Pack(const FName& name) {
    static_assert(sizeof(FName) == sizeof(std::uint64_t));
    Key key;
    std::memcpy(&key.raw, &name, sizeof(key.raw));
    return key;
}

inline std::uint8_t* RoleValue(std::uintptr_t image, void* playerState, std::uint64_t role) {
    auto* map = static_cast<std::uint8_t*>(playerState) + MapOffset;
    std::int32_t id = -1;
    using Find = std::int32_t* (*)(void*, std::int32_t*, std::uint64_t);
    reinterpret_cast<Find>(image + FindRoleRva)(map, &id, role);
    std::uint8_t* data{};
    std::memcpy(&data, map, sizeof(data));
    return id < 0 || !data ? nullptr : data + static_cast<std::uintptr_t>(id) * ElementSize + 8;
}

inline std::uint64_t NameAt(const std::uint8_t* value, std::uintptr_t offset) {
    std::uint64_t raw{};
    std::memcpy(&raw, value + offset, sizeof(raw));
    return raw;
}
inline void SetName(std::uint8_t* value, std::uintptr_t offset, const Key& name) {
    std::memcpy(value + offset, &name.raw, sizeof(name.raw));
}
// TSparseArray：已分配数在 +8，空闲数在 +0x34。
inline std::int32_t WeaponCount(const std::uint8_t* value) {
    std::int32_t allocated{}, unused{};
    std::memcpy(&allocated, value + Weapons + 8, sizeof(allocated));
    std::memcpy(&unused, value + Weapons + 0x34, sizeof(unused));
    return allocated - unused;
}

// 与 0x1791F00 对每把枪的调用顺序一致：取枪条目、整枪外观、逐槽配件及其外观、挂饰。
template<class Intern>
bool ApplyWeapon(std::uintptr_t image, std::uint8_t* value, const spawn::Weapon& weapon, Intern&& intern) {
    if (!weapon.Present()) return false;
    using WeaponFn = void* (*)(void*, const Key*);
    using PairFn = void (*)(void*, const Key*, const Key*);
    using PartFn = void (*)(void*, const std::uint8_t*, const Key*);
    using LookFn = void (*)(void*, const Key*, const Key*, const std::uint8_t*);
    using NameFn = void (*)(void*, const Key*);
    // 先解析全部名称；任何一项无效都在触碰原生表之前抛出，不留下半份配置。
    struct PartKeys { Key part, skin, paint, special; };
    const Key id = Pack(intern(weapon.id)), skin = Pack(intern(weapon.appearance.skin)),
        paint = Pack(intern(weapon.appearance.paint)), ornament = Pack(intern(weapon.ornament));
    std::array<PartKeys, 10> parts;
    for (std::size_t index = 0; index < weapon.parts.size(); ++index) {
        const auto& saved = weapon.parts[index];
        parts[index] = {Pack(intern(saved.id)), Pack(intern(saved.skin)), Pack(intern(saved.paint)), Pack(intern(saved.special))};
    }
    void* entry = reinterpret_cast<WeaponFn>(image + WeaponRva)(value, &id);
    if (!entry) return false;
    reinterpret_cast<PairFn>(image + WeaponSkinRva)(entry, &skin, &paint);
    const std::uint8_t specialClass[8]{4}, skinClass[8]{1};
    for (std::size_t index = 0; index < parts.size(); ++index) {
        const std::uint8_t slot[8]{static_cast<std::uint8_t>(index + 1)};
        reinterpret_cast<PartFn>(image + PartRva)(entry, slot, &parts[index].part);
        reinterpret_cast<LookFn>(image + PartLookRva)(entry, &parts[index].part, &parts[index].special, specialClass);
        reinterpret_cast<LookFn>(image + PartLookRva)(entry, &parts[index].part, &parts[index].skin, skinClass);
        reinterpret_cast<PairFn>(image + PartPaintRva)(entry, &parts[index].part, &parts[index].paint);
    }
    reinterpret_cast<NameFn>(image + OrnamentRva)(entry, &ornament);
    return true;
}

// 只写本人明确保存过的字段；缺失字段保留该干员的原生默认外观。
template<class Intern>
int ApplyCharacter(std::uint8_t* value, const spawn::CharacterAppearance& look, Intern&& intern) {
    int written = 0;
    const std::pair<const std::optional<spawn::Name>*, std::uintptr_t> fields[]{
        {&look.suit, Suit}, {&look.painting, SuitPaint}, {&look.badge, Badge}, {&look.head, Head}};
    for (const auto& [name, offset] : fields)
        if (name->has_value()) { SetName(value, offset, Pack(intern(**name))); ++written; }
    return written;
}
}

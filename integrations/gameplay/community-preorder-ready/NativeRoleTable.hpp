#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace brt::preorder::native {
struct RawArray { std::uintptr_t data{}; std::int32_t count{}, capacity{}; };
struct RawInventory { RawArray slots, items; };
struct RawTable { RawArray roles, inventories; };
using RoleKey = std::array<std::byte, 8>;
static_assert(sizeof(RawArray) == 16 && sizeof(RawInventory) == 32 && sizeof(RawTable) == 32);

inline bool Valid(const RawArray& array, std::size_t maximum = 64) {
    return array.count >= 0 && static_cast<std::size_t>(array.count) <= maximum &&
        array.capacity >= array.count && array.capacity <= 4096 && (array.capacity == 0 || array.data != 0);
}

template<class Read>
bool Bytes(Read&& read, std::uintptr_t address, void* destination, std::size_t bytes) {
    return bytes == 0 || (address != 0 && address <= (std::numeric_limits<std::uintptr_t>::max)() - bytes &&
        read(address, destination, bytes));
}
template<class T, class Read>
bool Value(Read&& read, std::uintptr_t address, T& value) { return Bytes(read, address, &value, sizeof(value)); }

// 仅接受两份实局转储所对应的 Steam Build 14530908；版本不符时不接触私有表。
template<class Read>
bool SupportedImage(std::uintptr_t base, Read&& read) {
    if (!base || base > (std::numeric_limits<std::uintptr_t>::max)() - 0x0648C000) return false;
    std::uint16_t mz{}, machine{}, magic{};
    std::uint32_t peOffset{}, signature{}, timestamp{}, size{};
    if (!Value(read, base, mz) || mz != 0x5A4D || !Value(read, base + 0x3C, peOffset) ||
        peOffset < 0x40 || peOffset > 0x100000) return false;
    const auto pe = base + peOffset;
    if (!Value(read, pe, signature) || signature != 0x4550 || !Value(read, pe + 4, machine) || machine != 0x8664 ||
        !Value(read, pe + 8, timestamp) || timestamp != 0x6656D8D7 || !Value(read, pe + 24, magic) || magic != 0x20B ||
        !Value(read, pe + 24 + 56, size) || size != 0x0648C000) return false;
    constexpr std::array<std::uint8_t, 22> lookup{0x48,0x8B,0x8E,0xB8,0x05,0x00,0x00,0x49,0x8B,0xD6,0x48,0x81,0xC1,0xC0,0x06,0x00,0x00,0xE8,0x76,0x12,0x14,0x00};
    constexpr std::array<std::uint8_t, 14> keys{0x4C,0x8B,0x11,0x4C,0x63,0x41,0x08,0x49,0x8B,0xC2,0x4F,0x8D,0x0C,0xC2};
    std::array<std::uint8_t, lookup.size()> actualLookup{};
    std::array<std::uint8_t, keys.size()> actualKeys{};
    return Bytes(read, base + 0x15C1404, actualLookup.data(), actualLookup.size()) && actualLookup == lookup &&
        Bytes(read, base + 0x1702690, actualKeys.data(), actualKeys.size()) && actualKeys == keys;
}

// Read 在生产中是 ReadProcessMemory；这里只复制 POD，从不构造或析构游戏的拥有型容器。
template<class Read>
std::optional<std::vector<RoleKey>> ReadRoles(std::uintptr_t state, Read&& read) {
    if (state > (std::numeric_limits<std::uintptr_t>::max)() - 0x6C0) return {};
    RawTable table;
    if (!Value(read, state + 0x6C0, table) || !Valid(table.roles) || !Valid(table.inventories) ||
        table.roles.count == 0 || table.roles.count != table.inventories.count) return {};
    std::vector<RoleKey> roles(table.roles.count);
    std::vector<RawInventory> inventories(table.inventories.count);
    if (!Bytes(read, table.roles.data, roles.data(), roles.size() * sizeof(RoleKey)) ||
        !Bytes(read, table.inventories.data, inventories.data(), inventories.size() * sizeof(RawInventory))) return {};
    for (const auto& inventory : inventories) {
        if (!Valid(inventory.slots) || !Valid(inventory.items) || inventory.slots.count != inventory.items.count) return {};
        std::array<std::uint8_t, 64> slots{};
        std::array<RoleKey, 64> items{};
        if (!Bytes(read, inventory.slots.data, slots.data(), inventory.slots.count) ||
            !Bytes(read, inventory.items.data, items.data(), inventory.items.count * sizeof(RoleKey))) return {};
    }
    return roles;
}
}

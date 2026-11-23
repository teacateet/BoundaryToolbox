#include <windows.h>
#include "NativeRoleTable.hpp"
#undef max
#undef min
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>

using namespace brt::preorder::native;
struct Memory {
    std::map<std::uintptr_t, std::vector<std::uint8_t>> blocks;
    bool Read(std::uintptr_t address, void* destination, std::size_t count) const {
        auto found = blocks.upper_bound(address);
        if (found == blocks.begin()) return false;
        --found;
        const auto offset = address - found->first;
        if (offset > found->second.size() || count > found->second.size() - offset) return false;
        std::memcpy(destination, found->second.data() + offset, count);
        return true;
    }
    template<class T> void Put(std::uintptr_t address, const T& value) {
        auto found = blocks.upper_bound(address);
        if (found != blocks.begin()) {
            --found;
            const auto offset = address - found->first;
            if (offset <= found->second.size() && sizeof(value) <= found->second.size() - offset) {
                std::memcpy(found->second.data() + offset, &value, sizeof(value)); return;
            }
        }
        auto& bytes = blocks[address]; bytes.resize(sizeof(value));
        std::memcpy(bytes.data(), &value, sizeof(value));
    }
};

int main() {
    unsigned cases{};
    Memory memory;
    auto read = [&](std::uintptr_t address, void* destination, std::size_t count) { return memory.Read(address, destination, count); };
    constexpr std::uintptr_t image = 0x40000000, pe = image + 0x80, state = 0x100000;
    memory.blocks[image].resize(512);
    memory.Put(image, std::uint16_t{0x5A4D}); memory.Put(image + 0x3C, std::uint32_t{0x80});
    memory.Put(pe, std::uint32_t{0x4550}); memory.Put(pe + 4, std::uint16_t{0x8664});
    memory.Put(pe + 8, std::uint32_t{0x6656D8D7}); memory.Put(pe + 24, std::uint16_t{0x20B});
    memory.Put(pe + 80, std::uint32_t{0x0648C000});
    memory.Put(image + 0x15C1404, std::array<std::uint8_t, 22>{0x48,0x8B,0x8E,0xB8,0x05,0x00,0x00,0x49,0x8B,0xD6,0x48,0x81,0xC1,0xC0,0x06,0x00,0x00,0xE8,0x76,0x12,0x14,0x00});
    memory.Put(image + 0x1702690, std::array<std::uint8_t, 14>{0x4C,0x8B,0x11,0x4C,0x63,0x41,0x08,0x49,0x8B,0xC2,0x4F,0x8D,0x0C,0xC2});
    assert(SupportedImage(image, read)); ++cases;
    memory.Put(pe + 8, std::uint32_t{0x6656D8D8});
    assert(!SupportedImage(image, read)); ++cases;
    memory.Put(pe + 8, std::uint32_t{0x6656D8D7});
    memory.Put(image + 0x15C1404, std::uint8_t{0x90});
    assert(!SupportedImage(image, read)); ++cases;

    memory.Put(state + 0x6C0, RawTable{});
    assert(!ReadRoles(state, read)); ++cases;
    RawTable table{{0x110000, 1, 1}, {0x120000, 1, 1}};
    RoleKey key{}, missing{}; key[0] = std::byte{7}; missing[0] = std::byte{8};
    memory.Put(state + 0x6C0, table); memory.Put(table.roles.data, key);
    memory.Put(table.inventories.data, RawInventory{});
    const auto available = ReadRoles(state, read);
    assert(available && available->size() == 1 && available->front() == key); ++cases;
    assert(std::find(available->begin(), available->end(), missing) == available->end()); ++cases;
    table.inventories.count = 0; memory.Put(state + 0x6C0, table);
    assert(!ReadRoles(state, read)); ++cases;
    table.inventories.count = 1; table.roles.count = table.roles.capacity = 65; memory.Put(state + 0x6C0, table);
    assert(!ReadRoles(state, read)); ++cases;
    table.roles.count = table.roles.capacity = 1; memory.Put(state + 0x6C0, table);
    RawInventory inventory{{0x130000, 1, 1}, {}};
    memory.Put(table.inventories.data, inventory);
    assert(!ReadRoles(state, read)); ++cases;
    inventory.items = {0x140000, 1, 1}; memory.Put(table.inventories.data, inventory);
    memory.Put(inventory.slots.data, std::uint8_t{1}); memory.Put(inventory.items.data, key);
    assert(ReadRoles(state, read)); ++cases;
    table.inventories.data = 0xDEAD0000; memory.Put(state + 0x6C0, table);
    assert(!ReadRoles(state, read)); ++cases;
    assert(!ReadRoles(std::numeric_limits<std::uintptr_t>::max(), read)); ++cases;
    std::cout << "PASS " << cases << " native role table read scenarios\n";
}

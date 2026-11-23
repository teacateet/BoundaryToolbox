#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include "SDKHelper/UnrealContainers.hpp"
#include "FieldMenuRows.hpp"

using UC::TArray;
using namespace brt::field_menu;
struct Row { TArray<int> CharacterSlots, InventoryItems; };
struct Menu { TArray<int> RoleArray; TArray<Row> RoleInventoryNetworkConfigArray; };
struct Args { Menu ServerEquippingSaved; TArray<int> RoleIDs, OwnedQuotas; };
struct Saved { int id; std::array<int, 6> items; };
static int allocations{};
void* EngineAllocator(void* ptr, uint64_t size, uint32_t) {
    if (!size) { if (ptr) --allocations; std::free(ptr); return nullptr; }
    if (ptr) return std::realloc(ptr, size);
    ++allocations;
    void* result = std::malloc(size);
    if (result) std::memset(result, 0xcd, size);
    return result;
}
int main() {
    UC::FMemory::Init(reinterpret_cast<void*>(&EngineAllocator));
    std::array<Saved,6> saved{};
    const std::array slots{1,2,3,4,5,6};
    for (int i = 0; i < 6; ++i) saved[i] = {i+1,{100+i,200+i,0,400+i,500+i,600+i}};
    {
        Args a;
        ConstructedRows<Row> stock(2);
        for (int id : {1,9}) {
            a.ServerEquippingSaved.RoleArray.Add(id);
            auto& r = stock.New(); r.CharacterSlots.Add(2); r.InventoryItems.Add(888);
        }
        stock.Attach(a.ServerEquippingSaved.RoleInventoryNetworkConfigArray);
        for (int id : {2,1}) { a.RoleIDs.Add(id); a.OwnedQuotas.Add(10+id); }
        const int before = allocations;
        {
            auto p = Build(a,saved,[](auto id){return id;},slots);
            assert(p && p->restored == 2);
            const auto& m = p->args.ServerEquippingSaved;
            assert(m.RoleArray.Num() == 3 && m.RoleArray[0]==1 && m.RoleArray[1]==9 && m.RoleArray[2]==2);
            assert(m.RoleInventoryNetworkConfigArray[0].InventoryItems[2]==0 && m.RoleInventoryNetworkConfigArray[2].InventoryItems[0]==101);
            assert(m.RoleInventoryNetworkConfigArray[1].InventoryItems[0]==888);
            p->args.ServerEquippingSaved.RoleInventoryNetworkConfigArray[1].InventoryItems[0]=456;
            assert(a.ServerEquippingSaved.RoleInventoryNetworkConfigArray[1].InventoryItems[0]==888);
            assert(p->args.OwnedQuotas[0]==12 && p->args.OwnedQuotas[1]==11);
        }
        assert(allocations == before);
        try {
            Build(a,saved,[](int i){if(i==400) throw std::runtime_error("item"); return i;},slots);
            assert(false);
        } catch(const std::runtime_error&) { assert(allocations==before); }
        a.RoleIDs.Add(2); a.OwnedQuotas.Add(10);
        assert(!Build(a,saved,[](auto id){return id;},slots));
    }
    {
        Args a;
        for (int i=1;i<=6;++i) {a.RoleIDs.Add(i);a.OwnedQuotas.Add(i);}
        auto p=Build(a,saved,[](auto id){return id;},slots);
        assert(p && p->restored==6 && p->args.ServerEquippingSaved.RoleArray.Num()==6);
        a.OwnedQuotas.Add(7);
        assert(!Build(a,saved,[](auto id){return id;},slots));
    }
    assert(!allocations);
    std::puts("Owner field menu: scope, explicit empty, isolation, cleanup, malformed input and empty stock passed");
}

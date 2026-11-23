#pragma once
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace brt::field_menu {

// 该版本 SDK 的 TArray::Add 不会构造非平凡元素，Free 也不会析构元素。
// 固定容量内逐行构造并析构；原 RPC 内的嵌套数组只读，绝不复制其所有权。
template<class Row>
class ConstructedRows final : private UC::TArray<Row> {
    using Base = UC::TArray<Row>;
    Base* attached_{};
public:
    explicit ConstructedRows(int capacity) : Base(capacity) {
        if (capacity && !this->Data) throw std::bad_alloc{};
    }
    ConstructedRows(const ConstructedRows&) = delete;
    Row& New() {
        if (this->NumElements >= this->MaxElements) throw std::length_error("FieldMod rows");
        auto* row = std::construct_at(this->Data + this->NumElements);
        ++this->NumElements;
        return *row;
    }
    void Attach(Base& dest) {
        dest = std::move(static_cast<Base&>(*this));
        attached_ = &dest;
    }
    ~ConstructedRows() {
        if (attached_) static_cast<Base&>(*this) = std::move(*attached_);
        while (this->NumElements) std::destroy_at(this->Data + --this->NumElements);
    }
};

template<class Array>
bool Valid(const Array& a, int limit) {
    return a.Num() >= 0 && a.Num() <= limit && a.Num() <= a.Max() &&
        (!a.Num() || a.IsValidIndex(a.Num() - 1));
}
template<class Array>
bool Unique(const Array& a) {
    for (int i = 0; i < a.Num(); ++i)
        for (int j = 0; j < i; ++j) if (a[i] == a[j]) return false;
    return true;
}
template<class Array, class Name>
bool Contains(const Array& a, const Name& id) {
    for (int i = 0; i < a.Num(); ++i) if (a[i] == id) return true;
    return false;
}

template<class Args, class Row>
struct Packet {
    Args args{};
    ConstructedRows<Row> rows;
    int restored{};
    explicit Packet(int capacity) : rows(capacity) {}
};

template<class Args, class Saved, class Intern, class Slots>
auto Build(const Args& original, const Saved& saved, Intern intern, const Slots& slots) {
    using Row = std::remove_cvref_t<decltype(original.ServerEquippingSaved.RoleInventoryNetworkConfigArray[0])>;
    using Result = std::unique_ptr<Packet<Args, Row>>;
    const auto& stock = original.ServerEquippingSaved;
    if (saved.size() != 6 || !Valid(stock.RoleArray, 64) || !Unique(stock.RoleArray) ||
        !Valid(stock.RoleInventoryNetworkConfigArray, 64) || stock.RoleArray.Num() != stock.RoleInventoryNetworkConfigArray.Num() ||
        !Valid(original.RoleIDs, 64) || !Unique(original.RoleIDs) || !original.RoleIDs.Num() ||
        !Valid(original.OwnedQuotas, 64) || original.RoleIDs.Num() != original.OwnedQuotas.Num()) return Result{};
    for (int i = 0; i < stock.RoleArray.Num(); ++i) {
        const auto& r = stock.RoleInventoryNetworkConfigArray[i];
        if (!Valid(r.CharacterSlots, 64) || !Valid(r.InventoryItems, 64) || r.CharacterSlots.Num() != r.InventoryItems.Num()) return Result{};
    }
    Result p = std::make_unique<Packet<Args, Row>>(stock.RoleArray.Num() + original.RoleIDs.Num());
    auto& dst = p->args;
    for (int i = 0; i < original.RoleIDs.Num(); ++i) {
        dst.RoleIDs.Add(original.RoleIDs[i]); dst.OwnedQuotas.Add(original.OwnedQuotas[i]);
    }
    using Name = std::remove_cvref_t<decltype(original.RoleIDs[0])>;
    std::array<Name, 6> keys{};
    for (unsigned i = 0; i < keys.size(); ++i) keys[i] = intern(saved[i].id);
    auto append = [&](const Name& id, const Row* stockRow) {
        const auto where = std::find(keys.begin(), keys.end(), id);
        const bool use = where != keys.end() && Contains(original.RoleIDs, id);
        if (!use && !stockRow) return;
        auto& row = p->rows.New();
        dst.ServerEquippingSaved.RoleArray.Add(id);
        if (use) {
            const auto& items = saved[where - keys.begin()].items;
            for (unsigned j = 0; j < slots.size(); ++j) {
                row.CharacterSlots.Add(slots[j]); row.InventoryItems.Add(intern(items[j]));
            }
            ++p->restored;
        } else {
            for (int j = 0; j < stockRow->CharacterSlots.Num(); ++j) {
                row.CharacterSlots.Add(stockRow->CharacterSlots[j]); row.InventoryItems.Add(stockRow->InventoryItems[j]);
            }
        }
    };
    for (int i = 0; i < stock.RoleArray.Num(); ++i) append(stock.RoleArray[i], &stock.RoleInventoryNetworkConfigArray[i]);
    for (int i = 0; i < original.RoleIDs.Num(); ++i)
        if (!Contains(stock.RoleArray, original.RoleIDs[i])) append(original.RoleIDs[i], nullptr);
    if (!p->restored) return Result{};
    p->rows.Attach(dst.ServerEquippingSaved.RoleInventoryNetworkConfigArray);
    return p;
}
}

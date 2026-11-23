#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace brt::preorder {
struct Availability {
    bool connected{};
    bool archiveLoaded{};
    bool inventoryLoaded{};
    bool selected{};
};
enum class Progress { Waiting, Complete, Cancelled, Idle };

// 这里只保存自有值，绝不复制 SDK 的嵌套 TArray 或持有它们的数据指针。
template<class Key, class Role, std::size_t Count = 6>
class Gate {
    struct Snapshot { Key key; std::vector<Role> roles; };
    struct Request { Key key; std::array<Role, Count> roles; std::array<bool, Count> sent{}; };
    std::optional<Snapshot> native_;
    std::optional<Request> request_;
    std::uint64_t generation_{};
public:
    bool Observe(const Key& key, std::vector<Role> roles, int inventories) {
        native_.reset();
        if (roles.empty() || roles.size() > 64 || inventories < 0 || roles.size() != static_cast<std::size_t>(inventories)) return false;
        for (std::size_t i = 0; i < roles.size(); ++i)
            if (std::find(roles.begin(), roles.begin() + i, roles[i]) != roles.begin() + i) return false;
        native_.emplace(Snapshot{key, std::move(roles)});
        return true;
    }
    void Queue(const Key& key, const std::array<Role, Count>& roles) {
        if (request_ && request_->key == key) return;
        request_.emplace(Request{key, roles, {}});
        ++generation_;
    }
    bool Pending(const Key& key) const { return request_ && request_->key == key; }
    void Cancel() { request_.reset(); ++generation_; }

    template<class Submit>
    Progress Tick(const Key& key, Availability available, Submit&& submit) {
        if (!request_) return Progress::Idle;
        if (!(request_->key == key) || !available.connected || available.selected) {
            Cancel();
            return Progress::Cancelled;
        }
        if (!available.archiveLoaded || !available.inventoryLoaded || !native_ || !(native_->key == key)) return Progress::Waiting;
        const auto generation = generation_;
        for (std::size_t i = 0; i < Count; ++i) {
            if (!request_ || generation_ != generation) return Progress::Cancelled;
            if (!native_ || !(native_->key == key)) return Progress::Waiting;
            if (request_->sent[i] || std::find(native_->roles.begin(), native_->roles.end(), request_->roles[i]) == native_->roles.end()) continue;
            if (!submit(i)) return Progress::Waiting;
            if (!request_ || generation_ != generation) return Progress::Cancelled;
            request_->sent[i] = true;
        }
        if (!std::all_of(request_->sent.begin(), request_->sent.end(), [](bool sent) { return sent; })) return Progress::Waiting;
        Cancel();
        return Progress::Complete;
    }
};
}

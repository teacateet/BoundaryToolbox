#pragma once

#include "DeathMenu.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace BrtDeathMenuTravel
{
using BrtDeathMenu::Id;
using BrtDeathMenu::Time;

struct Receipt
{
    Id source{}, instance{}, player{};
    std::string target;
    Time issued{};
};

// 传递普通切图操作的身份。接收方只比较地址，永远不解引用旧世界或旧角色。
inline std::mutex receiptMutex;
inline std::atomic<bool> changed{};
inline std::uint64_t sequence{};
inline Receipt receipt;

inline std::uint64_t Begin()
{
    std::lock_guard lock(receiptMutex);
    receipt = {};
    changed.store(true);
    return ++sequence;
}

inline void Complete(std::uint64_t ticket, Id source, Id instance, Id local, const std::string& target)
{
    std::lock_guard lock(receiptMutex);
    if (ticket != sequence)
        return;
    receipt = {source, instance, local, target, BrtDeathMenu::Clock::now()};
    changed.store(true);
}

inline std::optional<Receipt> Take()
{
    if (!changed.load())
        return {};
    std::lock_guard lock(receiptMutex);
    if (!changed.exchange(false))
        return {};
    return std::move(receipt);
}
}

#pragma once
#include <Windows.h>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace brt::persona {
using Id = std::uint64_t;
using Clock = std::chrono::steady_clock;

inline bool Individual(Id id) {
    return (id >> 56) == 1 && ((id >> 52) & 15) == 1 &&
           ((id >> 32) & 0xfffff) == 1 && static_cast<std::uint32_t>(id) != 0;
}
template<class Settings>
bool DisplayEnabled(const Settings* settings) { return settings && !settings->CustomPlayerName; }
inline Id Account(std::string_view text) {
    if (text.size() != 17) return 0;
    Id id{};
    const auto parsed = std::from_chars(text.data(), text.data()+text.size(), id);
    return parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && Individual(id) ? id : 0;
}
inline Id Account(std::wstring_view text) {
    if (text.size()!=17) return 0;
    Id id{};
    for (wchar_t digit : text) {
        if (digit<L'0' || digit>L'9') return 0;
        id=id*10+static_cast<unsigned>(digit-L'0');
    }
    return Individual(id) ? id : 0;
}

// SteamFriends 返回 UTF-8；无法完整转换或不能作为单行文本显示时保留游戏原文。
inline std::optional<std::wstring> Decode(const char* text) {
    if (!text) return {};
    const auto bytes = strnlen_s(text,513);
    if (!bytes || bytes > 512) return {};
    const auto count = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,static_cast<int>(bytes),nullptr,0);
    if (count <= 0) return {};
    std::wstring value(count,L'\0');
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,static_cast<int>(bytes),value.data(),count) != count) return {};
    bool visible{};
    for (const auto c : value) {
        if (c < L' ' || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029) return {};
        visible |= c != L' ' && c != 0x3000 && c != 0x00a0;
    }
    return visible ? std::optional<std::wstring>(std::move(value)) : std::nullopt;
}

// 本人的名只从本人 Steam 会话读取。远端名只查当前比分行的 Steam 账户，
// 不使用对方提交的昵称，也不把本机名字用作他人的默认值。
template<class Friends>
std::optional<std::wstring> Read(Friends& api, Id loggedIn, Id localState, Id row, bool localRow) {
    if (!Individual(loggedIn) || loggedIn != localState || !Individual(row)) return {};
    if (localRow) return row == loggedIn ? Decode(api.Self()) : std::nullopt;
    if (row == loggedIn || api.Pending(row)) return {};
    auto result = Decode(api.Peer(row));
    return result && *result != L"[unknown]" ? result : std::nullopt;
}

// 同一个游戏状态只提交本人的名字；网络尚未复制时有限重试，永远不每帧发 RPC。
struct Publication {
    const void* state{};
    Id account{};
    std::wstring name;
    Clock::time_point next{};
    unsigned attempts{};
    bool ShouldSubmit(const void* current, Id id, const std::wstring& official,
                      const std::wstring& native, Clock::time_point now) {
        if (state != current || account != id || name != official) {
            state=current; account=id; name=official; attempts=0; next={};
        }
        if (native == official || now < next || attempts >= 3) return false;
        ++attempts; next=now+std::chrono::seconds(5); return true;
    }
};
}

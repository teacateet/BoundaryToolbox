#pragma once
#include "OwnerRoleMenu.hpp"
#include "RoomHttp.hpp"
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

namespace brt::login {

inline bool IsSteamAccount(std::string_view id) {
    return id.size() == 17 && id > "76561197960265728" && id <= "76561202255233023" &&
        id.find_first_not_of("0123456789") == std::string_view::npos;
}

inline bool ValidRows(const std::vector<spawn::SavedMenuRole>& rows) {
    const std::array<std::string_view, 6> regular{"PEACE","PROBE","FIXER","SNIPER","FORT","SPIKE"};
    if (rows.size() != regular.size()) return false;
    for (const auto& row : rows) {
        if (std::find(regular.begin(), regular.end(), row.id) == regular.end()) return false;
        for (const auto& item : row.items) {
            if (item.size() > 128 || item.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.") != std::string::npos) return false;
        }
    }
    return true;
}

// 只申请当前登录的选角快照，绝不占用这个房间登记出生的连接。
inline std::optional<std::vector<spawn::SavedMenuRole>> ReadAtLogin(const spawn::RoomHttp& room, const std::string& steam) {
    if (!IsSteamAccount(steam)) return {};
    GUID uuid{};
    wchar_t formatted[40]{};
    if (FAILED(CoCreateGuid(&uuid)) || StringFromGUID2(uuid, formatted, 40) != 39) return {};
    std::string request(36, ' ');
    std::transform(formatted + 1, formatted + 37, request.begin(), [](wchar_t c) { return static_cast<char>(c); });
    const spawn::Member member{room.RoomId(), request, steam};
    auto rows = spawn::ReadOwnerMenu(member, room.LoginMenu(member));
    if (!rows || !ValidRows(*rows)) return {};
    return rows;
}
}

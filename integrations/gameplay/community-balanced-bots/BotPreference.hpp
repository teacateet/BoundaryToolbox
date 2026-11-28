#pragma once
#include <Windows.h>
#pragma comment(lib, "Advapi32.lib")

namespace brt::bots {
// 只有同一 Windows 用户启动的本机房间读取此值，远端队员不能修改房主设置。
inline constexpr wchar_t SettingsKey[] = L"Software\\BoundaryResurrectionToolkit\\Game";
inline bool Enabled() {
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"FriendlyBots", RRF_RT_REG_DWORD,
                        nullptr, &value, &size) == ERROR_SUCCESS && value == 1;
}
inline bool Save(bool enabled) {
    DWORD value = enabled ? 1 : 0;
    return RegSetKeyValueW(HKEY_CURRENT_USER, SettingsKey, L"FriendlyBots", REG_DWORD,
                           &value, sizeof(value)) == ERROR_SUCCESS;
}
}

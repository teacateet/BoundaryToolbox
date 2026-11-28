#pragma once
#include <Windows.h>
#include <cwchar>

namespace brt::room_count {
// 当前 Windows 会话、当前游戏进程的一条采样。不写账号、地址或持久化日志。
class Sample {
    HANDLE handle = nullptr;
    volatile LONG64* view = nullptr;
    ULONGLONG due = 0;
public:
    bool Ready(ULONGLONG now) {
        if (now < due) return false;
        due = now + 2000;
        if (view) return true;
        wchar_t name[80]{};
        swprintf_s(name, L"Local\\BRT.RoomCount.v1.%lu", GetCurrentProcessId());
        HANDLE fresh = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8, name);
        if (!fresh) return false;
        if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(fresh); return false; }
        view = static_cast<volatile LONG64*>(MapViewOfFile(fresh, FILE_MAP_WRITE, 0, 0, 8));
        if (!view) { CloseHandle(fresh); return false; }
        handle = fresh;
        return true;
    }
    void Set(ULONGLONG now, unsigned role, int count) {
        InterlockedExchange64(view, count >= 0 && count <= 128
            ? static_cast<LONG64>((now << 16) | (role << 8) | static_cast<unsigned>(count)) : 0);
    }
    ~Sample() { if (view) UnmapViewOfFile(const_cast<LONG64*>(view)); if (handle) CloseHandle(handle); }
};

inline void Server(SDK::UNetDriver* driver) {
    static Sample sample;
    auto* world = driver ? driver->World : nullptr;
    if (!world || world->NetDriver != driver || driver->ServerConnection) return;
    const auto now = GetTickCount64();
    if (!sample.Ready(now)) return;
    int n = -1;
    // 只向所属世界的游戏驱动取连接。空房尚未创建比分状态，也没有已登录的真人。
    if (world->AuthorityGameMode) {
        n = 0;
        for (auto* c : driver->ClientConnections) {
            auto* pc = c ? c->PlayerController : nullptr;
            if (pc && pc->PlayerState && !pc->PlayerState->bIsInactive && !pc->PlayerState->bIsABot) ++n;
        }
    }
    sample.Set(now, 1, n);
}

inline void Client(SDK::UWorld* world, SDK::UPBLocalPlayer* local) {
    static Sample sample;
    const auto now = GetTickCount64();
    if (!sample.Ready(now)) return;
    int n = 0; bool self = false;
    auto* pc = local ? local->PlayerController : nullptr;
    if (world && !world->AuthorityGameMode && world->NetDriver && world->NetDriver->ServerConnection &&
        world->GameState && pc && pc->PlayerState) {
        for (auto* state : world->GameState->PlayerArray) {
            if (!state || state->bIsABot || state->bIsInactive) continue;
            ++n;
            if (state == pc->PlayerState) self = true;
        }
    }
    // 在地图载入、本人的状态未复制，或已回大厅时不报告旧值。
    sample.Set(now, 2, self ? n : -1);
}
}

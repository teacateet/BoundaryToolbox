#pragma once
#include "BotPreference.hpp"
#include <CommCtrl.h>
#pragma comment(lib, "Comctl32.lib")
namespace brt::bots {
using namespace SDK;
inline constexpr wchar_t Title[] = L"友方人机补位";
inline thread_local HWND menuWindow = nullptr;
inline thread_local bool requested = false, showing = false;
inline thread_local ULONGLONG retryAt = 0;
inline LRESULT CALLBACK MenuKey(HWND w, UINT msg, WPARAM key, LPARAM data, UINT_PTR id, DWORD_PTR) {
    // 保留原版 F7 反馈与 F8 直播模式，只监听大厅或军械库窗口的 F9。
    if (msg == WM_KEYDOWN && key == VK_F9 && !(data & (LPARAM(1) << 30))) requested = true;
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(w, MenuKey, id);
        menuWindow = nullptr; requested = false;
    }
    return DefSubclassProc(w, msg, key, data);
}
inline BOOL CALLBACK BindMenuWindow(HWND w, LPARAM) {
    wchar_t cls[32]{};
    GetClassNameW(w, cls, 32);
    if (!GetWindow(w, GW_OWNER) && wcscmp(cls, L"UnrealWindow") == 0 &&
        SetWindowSubclass(w, MenuKey, 1, 0)) { menuWindow = w; return FALSE; }
    return TRUE;
}
inline FText Text(const wchar_t* s) { return UKismetTextLibrary::Conv_StringToText(s); }
inline void Menu(UWorld* world, UPBLocalPlayer* local) {
    if (showing) return;
    if (!world || world->NetDriver || !world->AuthorityGameMode || !local || !local->PlayerController ||
        !(world->AuthorityGameMode->IsA(APBGameMode_Menu::StaticClass()) ||
          world->AuthorityGameMode->IsA(APBGameMode_ShowRoom::StaticClass()))) {
        if (menuWindow) RemoveWindowSubclass(menuWindow, MenuKey, 1);
        menuWindow = nullptr; requested = false; return;
    }
    if (!menuWindow && GetTickCount64() >= retryAt) {
        retryAt = GetTickCount64() + 1000;
        EnumThreadWindows(GetCurrentThreadId(), BindMenuWindow, 0);
    }
    if (!requested) return;
    requested = false;
    showing = true;
    auto* pm = static_cast<UPBPromptManager*>(USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController(
        local->PlayerController, UPBPromptManager::StaticClass()));
    if (pm) {
        TArray<FText> choices(2); choices.Add(Text(L"开启")); choices.Add(Text(L"关闭"));
        pm->PopUp(Text(Title), Text(Enabled() ? L"当前：开启。下次本机人机对局将为双方补齐空位。"
                                              : L"当前：关闭。开启后，下次本机人机对局将为双方补齐空位。"), choices, {});
    }
    showing = false;
}
inline void Click(UObject* sender, int option) {
    if (!sender || (option != 0 && option != 1) || !sender->IsA(UPBPopUpWidget::StaticClass())) return;
    auto* popup = static_cast<UPBPopUpWidget*>(sender);
    if (!popup->IsActivated() || popup->OptionTitleTexts.Num() != 2 ||
        UKismetTextLibrary::TextIsEmpty(popup->TitleText) ||
        !UKismetTextLibrary::EqualEqual_TextText(popup->TitleText, Text(Title))) return;
    if (!Save(option == 0)) ClientLog("[BRT-BOTS] failed to save friendly bot setting");
}
}

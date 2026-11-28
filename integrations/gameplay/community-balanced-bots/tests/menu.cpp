#include <Windows.h>
#include <cassert>
#include <string>
#include <vector>

// 游戏对象用替身，快捷键走真实 Win32 窗口，偏好走隔离的 HKCU。
namespace SDK {
using FText = std::wstring;
struct UKismetTextLibrary {
    static FText Conv_StringToText(const wchar_t* s) { return s; }
    static bool TextIsEmpty(const FText& s) { return s.empty(); }
    static bool EqualEqual_TextText(const FText& a,const FText& b) { return a==b; }
};
template<class T> struct TArray : std::vector<T> {
    explicit TArray(int n=0) { this->reserve(n); }
    void Add(T x) { this->push_back(x); }
    int Num() const { return static_cast<int>(this->size()); }
};
struct UObject { int type=0; bool IsA(int t) const {return t==type;} };
struct APBGameMode_Menu {static int StaticClass(){return 1;}};
struct APBGameMode_ShowRoom {static int StaticClass(){return 2;}};
struct UPBPopUpWidget : UObject {
    static int StaticClass(){return 3;}
    bool active=true; FText TitleText; TArray<FText> OptionTitleTexts;
    bool IsActivated(){return active;}
};
struct UWorld {void* NetDriver=nullptr; UObject* AuthorityGameMode=nullptr;};
struct UPBLocalPlayer {void* PlayerController=nullptr;};
struct UPBPromptManager {
    static int StaticClass(){return 4;}
    UPBPopUpWidget widget; FText description; int count=0;
    void PopUp(FText t,FText d,const TArray<FText>& options,void*) {
        ++count; widget.type=3; widget.TitleText=t; widget.OptionTitleTexts=options; description=d;
    }
};
inline UPBPromptManager prompt;
struct USubsystemBlueprintLibrary {
    static UPBPromptManager* GetLocalPlayerSubSystemFromPlayerController(void*,int){return &prompt;}
};
}
void ClientLog(const char*) { assert(false); }
#include "../BotMenu.hpp"

int main() {
    using namespace brt::bots;
    std::wstring sub=L"Software\\BrtBotMenuTest-"+std::to_wstring(GetCurrentProcessId());
    HKEY fixture{}; DWORD created{};
    assert(RegCreateKeyExW(HKEY_CURRENT_USER,sub.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&fixture,&created)==0 && created==REG_CREATED_NEW_KEY);
    assert(RegOverridePredefKey(HKEY_CURRENT_USER,fixture)==0);
    WNDCLASSW cls{}; cls.lpfnWndProc=DefWindowProcW; cls.lpszClassName=L"UnrealWindow"; cls.hInstance=GetModuleHandleW(nullptr);
    assert(RegisterClassW(&cls));
    HWND w=CreateWindowW(cls.lpszClassName,L"QA",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);
    assert(w);
    UObject mode{1}; UWorld world{nullptr,&mode}; UPBLocalPlayer local{w};
    auto tick=[&]{Menu(&world,&local);};
    auto press=[&](int key,LPARAM flags=0){SendMessageW(w,WM_KEYDOWN,key,flags);tick();SendMessageW(w,WM_KEYUP,key,0);};
    tick(); assert(menuWindow==w && !Enabled());
    press(VK_F7);press(VK_F8);assert(prompt.count==0);
    press(VK_F9);assert(prompt.count==1 && prompt.description.find(L"当前：关闭")!=FText::npos);
    assert(prompt.widget.OptionTitleTexts==std::vector<FText>({L"开启",L"关闭"}));
    press(VK_F9,LPARAM(1)<<30);assert(prompt.count==1);
    Click(&prompt.widget,0);assert(Enabled());
    press(VK_F9);assert(prompt.description.find(L"当前：开启")!=FText::npos);
    prompt.widget.active=false;Click(&prompt.widget,1);assert(Enabled());
    prompt.widget.active=true;prompt.widget.TitleText=L"其他菜单";Click(&prompt.widget,1);assert(Enabled());
    prompt.widget.TitleText=Title;Click(&prompt.widget,1);assert(!Enabled());
    world.NetDriver=w;tick();press(VK_F9);assert(prompt.count==2 && !menuWindow);
    DestroyWindow(w);RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(fixture);
    assert(RegDeleteTreeW(HKEY_CURRENT_USER,sub.c_str())==0);
}

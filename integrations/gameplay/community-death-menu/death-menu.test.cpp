#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace SDK
{
template<class T> struct Array : std::vector<T>
{
    using std::vector<T>::vector;
    int Num() const { return static_cast<int>(this->size()); }
};
using FName = std::wstring;
struct FKey { FName KeyName; };
struct FText { std::wstring value; };
struct FString { std::string value; std::string ToString() const { return value; } };
struct FInputActionKeyMapping { FName ActionName; FKey Key; bool bShift{}, bCtrl{}, bAlt{}, bCmd{}; };
enum class EPBPlayerLifeStatus { Death, Downed, Alive };
struct UClass { std::string path; std::string GetName() const { return path; } };
struct Subclass { UClass* ptr{}; UClass* Get() { return ptr; } };
struct UObject
{
    virtual ~UObject() = default;
    virtual int Type() const { return 0; }
    bool IsA(int type) const { return Type() == type; }
};
#define TEST_SDK_CLASS(id) static int StaticClass() { return id; } int Type() const override { return id; }
struct APBPlayerController;
struct UPBLocalPlayer;
struct APBPlayerState : UObject
{
    TEST_SDK_CLASS(1)
    EPBPlayerLifeStatus life = EPBPlayerLifeStatus::Death;
    FName SelectedCharacterID{L"PROBE"};
    bool IsInPlayerLifeStatus(EPBPlayerLifeStatus expected) const { return life == expected; }
};
struct APBPlayerController : UObject
{
    TEST_SDK_CLASS(2)
    UPBLocalPlayer* Player{};
    UObject* PlayerState{};
    void* PlayerInput{};
    bool local{true}, selectable{true}, bShowSelectRole{}, menu{};
    std::set<FName> pressed, down;
    int openings{};
    int closings{}, gameFocus{};
    int restartRequests{};
    FName StateName{L"Inactive"};
    void ServerRestartPlayer() { ++restartRequests; }
    bool bShowMouseCursor{};
    bool IsLocalPlayerController() const { return local; }
    bool CanSelectRole() const { return selectable; }
    bool IsInGameMenuOpened() const { return menu; }
    void CloseExitMatchInGameMenu() { menu = false; }
    bool WasInputKeyJustPressed(const FKey& k) const { return pressed.count(k.KeyName) > 0; }
    bool IsInputKeyDown(const FKey& k) const { return down.count(k.KeyName) > 0; }
    void OpenInGameSelectRoleMenu() { ++openings; bShowSelectRole = true; bShowMouseCursor = true; }
    void CloseInGameSelectRoleMenu() { ++closings; bShowSelectRole = false; }
};
struct UWidgetBlueprintLibrary
{
    static void SetInputMode_GameOnly(APBPlayerController* pc) { ++pc->gameFocus; }
};
struct UPBLocalPlayer { APBPlayerController* PlayerController{}; };
struct APBGameState : UObject
{
    TEST_SDK_CLASS(3)
    Subclass GameModeClass;
    bool started{true}, ended{}, round{true};
    bool HasMatchStarted() const { return started; }
    bool HasMatchEnded() const { return ended; }
    bool IsRoundInProgress() const { return round; }
};
struct NetConnection { UObject* OwningActor{}; APBPlayerController* PlayerController{}; };
struct NetDriver { NetConnection* ServerConnection{}; };
struct GameInstance { Array<UPBLocalPlayer*> LocalPlayers; };
struct UWorld
{
    GameInstance* OwningGameInstance{};
    NetDriver* NetDriver{};
    APBGameState* GameState{};
    static inline UWorld* current{};
    static UWorld* GetWorld() { return current; }
};
struct UCommonActivatableWidget : UObject
{
    UClass typeName{}; UClass* Class = &typeName;
    APBPlayerController* owner{};
    bool active{true};
    APBPlayerController* GetOwningPlayer() { return owner; }
    bool IsActivated() { return active; }
    void DeactivateWidget() { active = false; }
};
struct Stack { UCommonActivatableWidget* current{}; UCommonActivatableWidget* GetActiveWidget() { return current; } };
struct UUMG_Prompt_C : UCommonActivatableWidget
{
    TEST_SDK_CLASS(4)
    FText TitleText, DescriptionText;
    Array<FText> OptionTitleTexts;
    int back{};
    std::function<void()> onBack;
    void OnOptionButtonClick(int button) { if(button==1 && onBack) onBack(); }
    bool BP_OnHandleBackAction() { ++back; if (onBack) onBack(); return false; }
};
struct UPBMainMenuLayout : UCommonActivatableWidget { Stack *PromptStack{}, *MenuStack{}, *InGameMenuStack{}; };
struct UPBMainMenuManager : UObject { TEST_SDK_CLASS(5) UPBMainMenuLayout* MainMenuLayout{}; };
struct UPBMainMenuManager_BP_C : UPBMainMenuManager
{
    TEST_SDK_CLASS(7)
    void GetTopPromptWidget(UCommonActivatableWidget** out) { *out=MainMenuLayout->PromptStack->GetActiveWidget(); }
    void GetTopMenuWidget(UCommonActivatableWidget** out) { *out=MainMenuLayout->MenuStack->GetActiveWidget(); }
    void GetTopInGameWidget(UCommonActivatableWidget** out) { *out=MainMenuLayout->InGameMenuStack->GetActiveWidget(); }
};
struct UPBOptions : UObject
{
    TEST_SDK_CLASS(6)
    Array<FKey> keys;
    Array<FKey> GetKeysFromActionName(const FName&) { return {}; }
    Array<FKey> GetCurrentDeviceKeysOfInput(const FName& name, float) { return name == L"InGameMenu" ? keys : Array<FKey>{}; }
};
struct UInputSettings
{
    Array<FInputActionKeyMapping> ActionMappings;
    static inline UInputSettings* current{};
    static UInputSettings* GetInputSettings() { return current; }
};
struct USubsystemBlueprintLibrary
{
    static inline UPBOptions* options{};
    static inline UPBMainMenuManager_BP_C* manager{};
    static inline APBPlayerController* local{};
    static UObject* GetEngineSubsystem(int type) { return type == UPBOptions::StaticClass() ? options : nullptr; }
    static UObject* GetLocalPlayerSubSystemFromPlayerController(APBPlayerController* pc, int type)
    { return pc == local && type == UPBMainMenuManager_BP_C::StaticClass() ? manager : nullptr; }
};
struct UKismetSystemLibrary
{
    static inline std::wstring command;
    static std::wstring GetCommandLine() { return command; }
    static bool ParseParam(const std::wstring& args, const std::wstring& key) { return args == L"-" + key; }
    static FString GetPathName(UClass* type) { return {type->path}; }
};
struct UKismetStringLibrary { static FName Conv_StringToName(const wchar_t* n) { return n; } };
struct UKismetTextLibrary
{
    static inline bool chinese{};
    static bool FindTextInLocalizationTable(const std::wstring& ns, const std::wstring& key, FText* result)
    {
        static const std::map<std::wstring, std::pair<std::wstring,std::wstring>> values{
            {L"Other_PopUp_LeaveMatch.Button", {L"LEAVE MATCH",L"离开比赛"}},
            {L"Other_PopUp_ConfirmOperation.Title", {L"PLEASE CONFIRM YOUR OPERATION",L"请确认您的操作"}},
            {L"Other_PopUp_Confirm.Title", {L"CONFIRM",L"确认"}}, {L"Other_PopUp_Cancel.Title",{L"CANCEL",L"取消"}}
        };
        static const std::map<std::wstring, std::pair<std::wstring,std::wstring>> page{
            {L"LEAVE MATCH",{L"LEAVE MATCH",L"离开比赛"}}, {L"CONFIRM",{L"CONFIRM",L"确定"}},
            {L"CANCEL",{L"CANCEL",L"取消"}}, {L"PLEASE CONFIRM YOUR OPERATION",{L"PLEASE CONFIRM YOUR OPERATION",L"请确认您的操作"}},
            {L"PLEASE CONFIRM YOUR COMMAND",{L"PLEASE CONFIRM YOUR COMMAND",L"请确认您的操作"}}
        };
        if (ns == L"PopUpPage")
        {
            auto it=page.find(key);
            if(it==page.end()) return false;
            result->value = chinese ? it->second.second : it->second.first;
            return true;
        }
        auto it = values.find(key);
        if (ns != L"PopUp" || it == values.end()) return false;
        result->value = chinese ? it->second.second : it->second.first;
        return true;
    }
    static bool EqualEqual_TextText(const FText& a,const FText& b) { return a.value == b.value; }
    static bool TextIsEmpty(const FText& t) { return t.value.empty(); }
};
}

#include "death-menu-entry.inc"

using namespace SDK;
using namespace std::chrono_literals;
struct Scene
{
    GameInstance instance;
    UClass mode{"/Game/Online/GameMode/BP_PBGameMode_Rush_PVE_Normal.BP_PBGameMode_Rush_PVE_Normal_C"};
    APBPlayerState state, alternateState;
    APBPlayerController pc, other;
    UPBLocalPlayer local{&pc};
    NetConnection connection{&pc, &pc}, anotherConnection{&pc, &pc};
    NetDriver driver{&connection};
    APBGameState match;
    UWorld origin{&instance, nullptr, nullptr}, world{&instance, &driver, &match}, replacement{&instance, &driver, &match};
    UWorld* presented{&world};
    UUMG_Prompt_C leave, otherPrompt;
    UCommonActivatableWidget menu;
    Stack prompts, mainMenu, inGame;
    UPBMainMenuLayout layout;
    UPBMainMenuManager_BP_C manager;
    UPBOptions options;
    UInputSettings inputs;
    Scene(bool chinese=true)
    {
        UKismetTextLibrary::chinese = chinese;
        instance.LocalPlayers = {&local};
        leave.TitleText.value = chinese ? L"离开比赛" : L"LEAVE MATCH";
        leave.DescriptionText.value = chinese ? L"请确认您的操作" : L"PLEASE CONFIRM YOUR OPERATION";
        leave.OptionTitleTexts = {{chinese ? L"确定" : L"CONFIRM"}, {chinese ? L"取消" : L"CANCEL"}};
        leave.owner = &pc;
        leave.onBack = [&]{ prompts.current = nullptr; };
        otherPrompt = leave;
        otherPrompt.TitleText.value = chinese ? L"退出游戏" : L"QUIT GAME";
        layout.owner = &pc;
        layout.PromptStack = &prompts;
        layout.MenuStack = &mainMenu;
        layout.InGameMenuStack = &inGame;
        manager.MainMenuLayout = &layout;
        USubsystemBlueprintLibrary::manager = &manager;
        USubsystemBlueprintLibrary::options = &options;
        USubsystemBlueprintLibrary::local = &pc;
        UInputSettings::current = &inputs;
        inputs.ActionMappings.push_back({L"InGameMenu",{L"Escape"}});
        pc.Player = &local;
        pc.PlayerInput = &inputs;
        pc.PlayerState = &state;
        match.GameModeClass.ptr = &mode;
        travel();
    }
    void travel(const std::string& target="127.0.0.1:7908", bool optIn=true)
    {
        UKismetSystemLibrary::command = optIn ? L"-BrtLocalPveUi" : L"";
        EntryTravel(&origin, &local, &target);
    }
    void tick() { std::this_thread::sleep_for(9ms); UWorld::current=presented; EntryTick(presented, &local); }
    void key(const wchar_t* k=L"Escape") { pc.pressed = {k}; pc.down = {k}; }
    void release() { pc.down.clear(); pc.pressed.clear(); }
    void show(UCommonActivatableWidget* w) { prompts.current = w; }
    void play(const wchar_t* k=L"Escape")
    {
        tick(); key(k); show(&leave); tick(); release(); tick();
    }
    bool fixed() const { return leave.back == 1 && pc.openings == 1 && !prompts.current && otherPrompt.back == 0; }
    bool untouched() const { return leave.back == 0 && otherPrompt.back == 0 && pc.openings == 0; }
    void difficulty(const std::string& suffix)
    {
        const auto base = "BP_PBGameMode_Rush_PVE_" + suffix;
        mode.path = "/Game/Online/GameMode/" + base + "." + base + "_C";
    }
};

int main()
{
    int failed = 0;
    auto check = [&](const char* name,const std::function<bool(Scene&)>& test, bool chinese=true)
    {
        Scene scene(chinese);
        const bool ok = test(scene);
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        failed += !ok;
    };
    check("recorded Chinese Leave Match opens roles", [](Scene& s){ s.play(); return s.fixed(); });
    check("English Leave Match opens roles", [](Scene& s){ s.play(); return s.fixed(); }, false);
    check("second stock Chinese confirmation title", [](Scene& s){ s.leave.OptionTitleTexts[0].value=L"确认"; s.play(); return s.fixed(); });
    check("stock English command confirmation", [](Scene& s){ s.leave.DescriptionText.value=L"PLEASE CONFIRM YOUR COMMAND"; s.play(); return s.fixed(); }, false);
    check("easy round opens roles", [](Scene& s){ s.difficulty("Easy"); s.play(); return s.fixed(); });
    check("hard round opens roles", [](Scene& s){ s.difficulty("Hard"); s.play(); return s.fixed(); });
    check("remapped active device key", [](Scene& s){ s.options.keys={{L"F8"}}; s.play(L"F8"); return s.fixed(); });
    check("real gamepad action from engine table", [](Scene& s){ s.inputs.ActionMappings={{L"InGameMenu",{L"Gamepad_Special_Right"}}}; s.play(L"Gamepad_Special_Right"); return s.fixed(); });
    check("late original prompt after a physical edge", [](Scene& s){ s.tick(); s.key(); s.tick(); s.release(); s.show(&s.leave); s.tick(); s.tick(); return s.fixed(); });
    check("death press on an empty stack opens roles without a leave prompt", [](Scene& s){
        s.tick(); s.key(); s.tick(); if(s.pc.openings) return false;
        s.release(); s.tick(); s.tick(); return s.pc.openings==1 && !s.leave.back && !s.otherPrompt.back;
    });
    check("empty stack death press waits for original role permission", [](Scene& s){
        s.pc.selectable=false; s.tick(); s.key(); s.tick(); s.release(); s.tick();
        if(!s.untouched()) return false; s.pc.selectable=true; s.tick();
        return s.pc.openings==1 && !s.leave.back && !s.otherPrompt.back;
    });
    check("new modal cancels a pending empty stack death press", [](Scene& s){
        s.pc.selectable=false; s.tick(); s.key(); s.tick(); s.release(); s.tick();
        s.show(&s.otherPrompt); s.pc.selectable=true; s.tick(); s.show(nullptr); s.tick();
        return s.untouched();
    });
    check("prompt initializes after appearing", [](Scene& s){ s.tick(); s.key(); s.show(&s.leave); s.leave.active=false; s.tick(); s.release(); s.leave.active=true; s.tick(); s.tick(); return s.fixed(); });
    check("deferred close opens once after original removal", [](Scene& s){ s.leave.onBack={}; s.play(); if(s.pc.openings) return false; s.tick(); s.show(nullptr); s.tick(); s.tick(); return s.fixed(); });
    check("root without explicit owner uses local subsystem", [](Scene& s){ s.layout.owner=nullptr; s.play(); return s.fixed(); });
    check("modifier binding with required key", [](Scene& s){ s.inputs.ActionMappings[0].bCtrl=true; s.tick(); s.key(); s.pc.down.insert(L"LeftControl"); s.show(&s.leave); s.tick(); s.release(); s.tick(); return s.fixed(); });
    check("pressed recomputes active widget without reentry", [](Scene& s){ s.leave.onBack=[&]{EntryTick(s.presented,&s.local); s.show(nullptr);}; s.play(); return s.fixed(); });
    check("does not confirm or dismiss Quit Game", [](Scene& s){ s.tick(); s.key(); s.show(&s.otherPrompt); s.tick(); s.tick(); return s.untouched() && s.prompts.current==&s.otherPrompt; });
    check("preexisting Leave Match stays open", [](Scene& s){ s.show(&s.leave); s.tick(); s.key(); s.tick(); s.tick(); return s.untouched(); });
    check("unrelated menu key", [](Scene& s){ s.play(L"R"); return s.untouched(); });
    check("Escape does not override a remap", [](Scene& s){ s.options.keys={{L"F8"}}; s.play(); return s.untouched(); });
    check("modifier binding without required key", [](Scene& s){ s.inputs.ActionMappings[0].bCtrl=true; s.play(); return s.untouched(); });
    check("unbound action", [](Scene& s){ s.inputs.ActionMappings.clear(); s.play(); return s.untouched(); });
    check("alive player keeps normal menu", [](Scene& s){ s.state.life=EPBPlayerLifeStatus::Alive; s.play(); return s.untouched(); });
    check("downed player keeps normal menu", [](Scene& s){ s.state.life=EPBPlayerLifeStatus::Downed; s.play(); return s.untouched(); });
    check("role selection disallowed", [](Scene& s){ s.pc.selectable=false; s.play(); return s.untouched(); });
    check("death press during cooldown opens roles when allowed", [](Scene& s){
        s.pc.selectable=false; s.play(); if(!s.untouched()) return false;
        s.pc.selectable=true; s.tick(); s.tick(); return s.fixed();
    });
    check("dismissed leave during cooldown stays dismissed", [](Scene& s){
        s.pc.selectable=false; s.play(); s.show(nullptr); s.tick();
        s.pc.selectable=true; s.tick(); return s.untouched();
    });
    check("different prompt during cooldown is untouched", [](Scene& s){
        s.pc.selectable=false; s.play(); s.show(&s.otherPrompt); s.pc.selectable=true;
        s.tick(); s.show(nullptr); s.tick(); return s.untouched();
    });
    check("new deliberate press cancels cooldown gesture", [](Scene& s){
        s.pc.selectable=false; s.play(); s.key(); s.pc.selectable=true;
        s.tick(); s.release(); s.tick(); return s.untouched();
    });
    check("cooldown press cannot carry through respawn", [](Scene& s){
        s.pc.selectable=false; s.play(); s.state.life=EPBPlayerLifeStatus::Alive; s.tick();
        s.state.life=EPBPlayerLifeStatus::Death; s.pc.selectable=true;
        std::this_thread::sleep_for(32ms); s.tick(); return s.untouched();
    });
    check("two deaths each open roles only on their own press", [](Scene& s){
        s.play(); if(!s.fixed()) return false;
        s.state.life=EPBPlayerLifeStatus::Alive; s.tick(); s.pc.bShowSelectRole=false;
        s.state.life=EPBPlayerLifeStatus::Death; s.pc.selectable=false;
        std::this_thread::sleep_for(32ms); s.play(); s.pc.selectable=true; s.tick(); s.tick();
        return s.leave.back==2 && s.pc.openings==2 && !s.prompts.current;
    });
    check("in combat with role page already shown", [](Scene& s){ s.pc.bShowSelectRole=true; s.play(); return s.untouched(); });
    check("existing menu stack", [](Scene& s){ s.mainMenu.current=&s.menu; s.play(); return s.untouched(); });
    check("existing ingame stack", [](Scene& s){ s.inGame.current=&s.menu; s.play(); return s.untouched(); });
    check("previously open native menu", [](Scene& s){ s.pc.menu=true; s.play(); return s.untouched(); });
    check("no local launch opt-in", [](Scene& s){ s.travel("127.0.0.1:7908",false); s.play(); return s.untouched(); });
    check("remote travel", [](Scene& s){ s.travel("192.0.2.5:7908"); s.play(); return s.untouched(); });
    check("malformed loopback or port", [](Scene& s){ for(auto x:{"127.0.0.1","127.0.0.1:0","127.0.0.1:65536","127.0.0.1:3?option","127.0.0.1.e:3"}){ s.travel(x); s.play(); } return s.untouched(); });
    check("PvP mode", [](Scene& s){ s.mode.path="/Game/Online/GameMode/BP_PBGameMode_Rush.BP_PBGameMode_Rush_C"; s.play(); return s.untouched(); });
    check("training mode", [](Scene& s){ s.mode.path="/Game/ShootingRange/BP_ShootingRange_C"; s.play(); return s.untouched(); });
    check("match not started or ending", [](Scene& s){ s.match.started=false; s.play(); s.match.started=true; s.match.ended=true; s.play(); return s.untouched(); });
    check("intermission", [](Scene& s){ s.match.round=false; s.play(); return s.untouched(); });
    check("other local player ownership", [](Scene& s){ s.pc.Player=nullptr; s.play(); return s.untouched(); });
    check("other player connection", [](Scene& s){ s.connection.OwningActor=&s.other; s.connection.PlayerController=&s.other; s.play(); return s.untouched(); });
    check("other player's dialog", [](Scene& s){ s.leave.owner=&s.other; s.play(); return s.untouched(); });
    check("other player's root layout", [](Scene& s){ s.layout.owner=&s.other; s.play(); return s.untouched(); });
    check("wrong description or confirmation options", [](Scene& s){ s.leave.OptionTitleTexts[0].value=L"DELETE"; s.play(); return s.untouched(); });
    check("foreign prompt type", [](Scene& s){ s.tick(); s.key(); s.show(&s.menu); s.tick(); s.tick(); return s.untouched(); });
    check("held key from an earlier frame", [](Scene& s){ s.pc.down={L"Escape"}; s.tick(); s.pc.pressed={L"Escape"}; s.show(&s.leave); s.tick(); return s.untouched(); });
    check("new modal after correlation timeout", [](Scene& s){ s.tick(); s.key(); s.tick(); s.release(); std::this_thread::sleep_for(315ms); s.show(&s.leave); s.tick(); s.tick(); return s.untouched(); });
    check("different state during a gesture", [](Scene& s){ s.tick(); s.key(); s.tick(); s.pc.PlayerState=&s.alternateState; s.show(&s.leave); s.tick(); s.tick(); return s.untouched(); });
    check("new world does not inherit verified room", [](Scene& s){ s.tick(); s.presented=&s.replacement; s.play(); return s.untouched(); });
    check("new connection does not inherit verified room", [](Scene& s){ s.tick(); s.driver.ServerConnection=&s.anotherConnection; s.play(); return s.untouched(); });
    check("missing root stack", [](Scene& s){ s.layout.PromptStack=nullptr; s.play(); return s.untouched(); });
    check("second prompt interrupts queued opening", [](Scene& s){ s.leave.onBack=[&]{s.show(&s.otherPrompt);}; s.play(); s.show(nullptr); s.tick(); return s.leave.back==1 && s.otherPrompt.back==0 && !s.pc.openings; });
    check("pooled widget reused for Quit interrupts opening", [](Scene& s){ s.leave.onBack=[&]{s.leave.TitleText=s.otherPrompt.TitleText;}; s.play(); s.show(nullptr); s.tick(); return s.leave.back==1 && !s.pc.openings; });
    check("second intentional press interrupts deferred opening", [](Scene& s){ s.leave.onBack={}; s.play(); s.key(); s.tick(); s.show(nullptr); s.tick(); return s.leave.back==1 && !s.pc.openings; });
    check("becoming alive between close and open", [](Scene& s){ s.leave.onBack=[&]{s.show(nullptr); s.state.life=EPBPlayerLifeStatus::Alive;}; s.play(); return s.leave.back==1 && !s.pc.openings; });
    check("CanSelectRole revoked before opening", [](Scene& s){ s.leave.onBack=[&]{s.show(nullptr); s.pc.selectable=false;}; s.play(); return s.leave.back==1 && !s.pc.openings; });
#ifndef TEST_V084
    check("changed role returns game focus once", [](Scene& s){
        s.play(); s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick(); s.tick();
        return s.pc.closings==1 && !s.pc.bShowSelectRole && !s.pc.bShowMouseCursor && s.pc.gameFocus==1;
    });
    check("same role explicit confirmation returns focus", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        return s.pc.closings==1 && s.pc.gameFocus==1 && s.pc.restartRequests==1;
    });
    check("server role acknowledgment precedes closing", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PEACE"); s.tick();
        if(!s.pc.bShowSelectRole || s.pc.closings || s.pc.gameFocus) return false;
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        return s.pc.closings==1 && s.pc.gameFocus==1;
    });
    check("cancelled selection returns focus", [](Scene& s){
        s.play(); s.pc.bShowSelectRole=false; s.tick(); s.tick();
        return s.pc.gameFocus==1 && !s.pc.closings && !s.pc.restartRequests;
    });
    check("confirmed role deactivates only owned role widget", [](Scene& s){
        s.play(); s.menu.owner=&s.pc; s.menu.typeName.path="InGameOption_V2_C"; s.inGame.current=&s.menu;
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        if(s.menu.active || s.pc.gameFocus) return false;
        s.inGame.current=nullptr; s.tick(); return s.pc.gameFocus==1;
    });
    check("unrelated modal keeps focus", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.show(&s.otherPrompt); s.tick(); s.tick();
        return s.pc.bShowSelectRole && s.pc.bShowMouseCursor && !s.pc.closings && !s.pc.gameFocus;
    });
#endif
#ifdef TEST_V084
    check("release reconnect delivers the same local session", [](Scene& s){ EntryReconnect(&s.origin,&s.local,"127.0.0.1:7908"); s.play(); return s.fixed(); });
    check("other character ticks after local pawn is gone", [](Scene& s){ s.play(); return s.fixed(); });
    check("late remote return cannot overwrite latest travel", [](Scene& s){ EntryReordered(&s.origin,&s.local); s.play(); return s.untouched(); });
    check("simultaneous local players fail closed", [](Scene& s){ s.instance.LocalPlayers.push_back(&s.local); s.play(); return s.untouched(); });
#endif
    return failed ? 1 : 0;
}

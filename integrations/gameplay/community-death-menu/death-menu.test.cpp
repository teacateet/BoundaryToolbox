#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <string_view>
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
struct APBCharacter : UObject {
    APBPlayerController* owner{};
    UObject* PlayerState{};
    bool alive{true}, firstPerson{true};
    APBPlayerController* GetController() const { return owner; }
    bool IsAlive() const { return alive; }
    bool IsInKilledStatus() const { return !alive; }
    bool IsFirstPerson() const { return firstPerson; }
};
struct APBPlayerState : UObject
{
    TEST_SDK_CLASS(1)
    EPBPlayerLifeStatus life = EPBPlayerLifeStatus::Death;
    int deaths{};
    bool bOnlySpectator{}, bIsSpectator{}, bIsABot{};
    APBCharacter* PawnPrivate{};
    int GetDeaths() const { return deaths; }
    FName SelectedCharacterID{L"PROBE"};
    FName PossessedCharacterId{};
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
    int restartRequests{}, possessionChecks{};
    APBCharacter* pawn{};
    APBCharacter* AcknowledgedPawn{};
    UObject* view{};
    APBCharacter* GetPBCharacter() const { return pawn; }
    APBCharacter* K2_GetPawn() const { return pawn; }
    UObject* GetViewTarget() const { return view; }
    bool gameLocked{}, moveLocked{}, lookLocked{};
    bool IsGameInputIgnored() const { return gameLocked; }
    bool IsMoveInputIgnored() const { return moveLocked; }
    bool IsLookInputIgnored() const { return lookLocked; }
    void ResetIgnoreGameInput() { gameLocked=false; }
    void ResetIgnoreMoveInput() { moveLocked=false; }
    void ResetIgnoreLookInput() { lookLocked=false; }
    FName StateName{L"Inactive"};
    void ServerRestartPlayer() { ++restartRequests; }
    void ServerCheckClientPossessionReliable() { ++possessionChecks; }
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
    static void SetFocusToGameViewport() {}
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
    bool active{true}, visible{true};
    APBPlayerController* GetOwningPlayer() { return owner; }
    bool IsActivated() { return active; }
    bool IsVisible() const { return visible; }
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
    static bool IsValid(const UObject* object) { return object!=nullptr; }
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
    APBCharacter newPawn;
    void possess() {
        state.life=EPBPlayerLifeStatus::Alive; pc.StateName=L"Playing"; pc.pawn=&newPawn;
        state.PossessedCharacterId=state.SelectedCharacterID;
        state.PawnPrivate=&newPawn; newPawn.PlayerState=&state; pc.view=&newPawn;
        newPawn.owner=&pc; pc.gameLocked=pc.moveLocked=pc.lookLocked=true;
        pc.bShowMouseCursor=true;
    }
    void settle() { tick(); std::this_thread::sleep_for(205ms); tick(); }
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
    check("possession after early menu close restores all input", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        if(s.pc.restartRequests!=1) return false;
        s.possess(); s.settle(); s.tick();
        return s.pc.gameFocus==2 && !s.pc.bShowMouseCursor && !s.pc.gameLocked && !s.pc.moveLocked && !s.pc.lookLocked;
    });
    check("early native close waits for the acknowledged role", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PEACE"); s.pc.bShowSelectRole=false; s.tick();
        if(s.pc.gameFocus) return false;
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.possess(); s.settle();
        return s.pc.gameFocus==2 && !s.pc.gameLocked;
    });
    check("missing selection layout and input survive possession", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PEACE"); s.pc.PlayerInput=nullptr; s.tick();
        s.pc.PlayerInput=&s.inputs; s.manager.MainMenuLayout=nullptr; s.tick();
        s.manager.MainMenuLayout=&s.layout; s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        s.possess(); s.settle(); return s.pc.gameFocus==2 && !s.pc.lookLocked;
    });
    check("stock death selection restores a late input lock only once", [](Scene& s){
        s.state.deaths=1; s.pc.OpenInGameSelectRoleMenu(); s.tick();
        EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick(); s.possess(); s.settle();
        s.pc.moveLocked=true; s.tick(); if(s.pc.moveLocked || s.pc.gameFocus!=3) return false;
        s.pc.lookLocked=true; s.tick(); return s.pc.lookLocked && s.pc.gameFocus==3;
    });
    check("new modal after deployment retains its input", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick(); s.possess(); s.show(&s.otherPrompt);
        s.settle(); s.show(nullptr); s.settle(); return s.pc.gameLocked && s.pc.gameFocus==1;
    });
    check("foreign pawn never receives redeploy focus", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick(); s.possess(); s.newPawn.owner=&s.other;
        s.settle(); return s.pc.gameLocked && s.pc.gameFocus==1;
    });
    check("confirmed deployment waits for inactive before its single restart", [](Scene& s){
        s.pc.StateName=L"Playing"; s.play(); EntryConfirm(&s.pc,L"PEACE");
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        if(s.pc.bShowSelectRole || s.pc.restartRequests) return false;
        s.pc.StateName=L"Inactive"; s.tick(); s.tick();
        s.possess(); s.settle(); return s.pc.restartRequests==1 && !s.pc.moveLocked && !s.pc.lookLocked;
    });
    check("pending death restart cannot cross a role change", [](Scene& s){
        s.pc.StateName=L"Playing"; s.play(); EntryConfirm(&s.pc,L"PEACE");
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        s.state.SelectedCharacterID=L"PROBE"; s.pc.StateName=L"Inactive"; s.tick(); s.tick();
        return s.pc.closings==1 && !s.pc.restartRequests;
    });
    check("unacknowledged client restart is not issued twice", [](Scene& s){
        s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick(); s.tick();
        if(s.pc.restartRequests!=1) return false;
        s.pc.StateName=L"Playing"; s.tick(); s.pc.StateName=L"Inactive"; s.tick();
        return s.pc.restartRequests==1;
    });
    check("blocked restart never follows another death", [](Scene& s){
        s.pc.StateName=L"Playing"; s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        if(s.pc.closings!=1) return false;
        ++s.state.deaths; s.pc.StateName=L"Inactive"; s.tick(); s.tick();
        return !s.pc.restartRequests;
    });
    check("native death menu can open confirm and close between samples", [](Scene& s){
        s.state.deaths=1; s.tick(); s.pc.StateName=L"Playing";
        s.pc.OpenInGameSelectRoleMenu(); EntryConfirm(&s.pc,L"PEACE"); s.pc.CloseInGameSelectRoleMenu();
        s.tick(); if(s.pc.restartRequests || s.pc.gameFocus) return false;
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.pc.StateName=L"Inactive"; s.tick();
        s.possess(); s.settle();
        return s.pc.restartRequests==1 && !s.pc.gameLocked && !s.pc.moveLocked && !s.pc.lookLocked && !s.pc.bShowMouseCursor;
    });
    check("same frame duplicate deployment of the previous role restarts only once", [](Scene& s){
        s.state.deaths=1; s.tick(); s.pc.OpenInGameSelectRoleMenu();
        EntryConfirm(&s.pc,L"PROBE"); EntryConfirm(&s.pc,L"PROBE"); s.pc.CloseInGameSelectRoleMenu();
        s.tick(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.possess(); s.settle();
        return s.pc.restartRequests==1 && !s.pc.moveLocked && !s.pc.lookLocked;
    });
    check("rapid choices wait for the last confirmed role", [](Scene& s){
        s.state.deaths=1; s.tick(); s.pc.OpenInGameSelectRoleMenu();
        EntryConfirm(&s.pc,L"PEACE"); EntryConfirm(&s.pc,L"SPIKE"); s.pc.CloseInGameSelectRoleMenu();
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); if(s.pc.restartRequests || s.pc.gameFocus) return false;
        s.state.SelectedCharacterID=L"SPIKE"; s.tick(); s.possess(); s.settle();
        return s.pc.restartRequests==1 && !s.pc.gameLocked;
    });
    check("transient modal before possession does not consume a confirmed deployment", [](Scene& s){
        s.state.deaths=1; s.pc.StateName=L"Playing"; s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        if(s.pc.restartRequests || s.pc.gameFocus!=1) return false;
        s.show(&s.otherPrompt); s.tick(); s.pc.StateName=L"Inactive"; s.show(nullptr); s.tick();
        s.possess(); s.settle();
        if(s.otherPrompt.back || s.pc.restartRequests!=1 || s.pc.moveLocked || s.pc.lookLocked) return false;
        // 真正的弹窗也可以跨越出生：仍在栈顶时不能夺焦点，玩家经原生“取消”后才交还。
        Scene after; after.state.deaths=1; after.pc.StateName=L"Playing"; after.play();
        EntryConfirm(&after.pc,L"PROBE"); after.tick(); after.tick();
        after.show(&after.otherPrompt); after.tick(); after.possess(); after.settle();
        if(after.prompts.current!=&after.otherPrompt || !after.pc.bShowMouseCursor || !after.pc.lookLocked) return false;
        after.otherPrompt.OnOptionButtonClick(1); after.settle();
        if(after.prompts.current || after.pc.bShowMouseCursor || after.pc.moveLocked || after.pc.lookLocked) return false;
        // 实局中角色 ID 已复制，原生对话框退栈约 150 ms 后又回来，此时还没有可控制的角色。
        Scene gap; gap.state.deaths=2; gap.pc.StateName=L"Playing"; gap.play();
        EntryConfirm(&gap.pc,L"PROBE"); gap.tick(); gap.tick(); gap.show(&gap.leave); gap.tick();
        gap.state.life=EPBPlayerLifeStatus::Alive; gap.state.PossessedCharacterId=L"PROBE"; gap.tick();
        gap.show(nullptr); gap.tick(); std::this_thread::sleep_for(140ms);
        gap.show(&gap.leave); gap.tick(); gap.possess(); gap.settle();
        if(gap.prompts.current!=&gap.leave || !gap.pc.lookLocked)return false;
        gap.leave.OnOptionButtonClick(1); gap.settle();
        return !gap.prompts.current && !gap.pc.lookLocked && !gap.pc.moveLocked && !gap.pc.bShowMouseCursor;
    });
    check("repeat escape on an empty stack does not open a second unconfirmed selection", [](Scene& s){
        s.state.deaths=1; s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        s.tick(); s.key(); s.tick(); s.release(); s.tick(); s.possess(); s.settle();
        return s.pc.openings==1 && s.pc.restartRequests==1 && !s.pc.lookLocked;
    });
    check("first life rapid confirmation keeps original behavior", [](Scene& s){
        s.state.deaths=0; s.tick(); s.pc.OpenInGameSelectRoleMenu();
        EntryConfirm(&s.pc,L"PEACE"); s.pc.CloseInGameSelectRoleMenu(); s.state.SelectedCharacterID=L"PEACE";
        s.tick(); s.possess(); s.settle(); return !s.pc.restartRequests && !s.pc.gameFocus && s.pc.gameLocked;
    });
    check("foreign same frame confirmation cannot assume ownership", [](Scene& s){
        s.state.deaths=1; s.tick(); s.pc.OpenInGameSelectRoleMenu();
        EntryConfirm(&s.other,L"PROBE"); s.pc.CloseInGameSelectRoleMenu();
        s.tick(); s.possess(); s.settle(); return !s.pc.restartRequests && !s.pc.gameFocus && s.pc.gameLocked;
    });
    check("rapid confirmation after connection replacement does not inherit local eligibility", [](Scene& s){
        s.state.deaths=1; s.tick(); s.driver.ServerConnection=&s.anotherConnection;
        s.pc.OpenInGameSelectRoleMenu(); EntryConfirm(&s.pc,L"PROBE"); s.pc.CloseInGameSelectRoleMenu();
        s.tick(); s.possess(); s.settle(); return !s.pc.restartRequests && !s.pc.gameFocus && s.pc.gameLocked;
    });
    check("nested widget callbacks cannot consume deployment before confirm returns", [](Scene& s){
        s.state.deaths=1; s.tick(); s.pc.OpenInGameSelectRoleMenu(); s.tick();
        EntryRoleAction([&]{
            s.pc.CloseInGameSelectRoleMenu(); s.tick();
            EntryRoleAction([&]{ EntryConfirm(&s.pc,L"PROBE"); s.tick(); });
            s.tick();
        });
        if(s.pc.restartRequests || s.pc.gameFocus) return false;
        s.tick(); s.possess(); s.settle(); return s.pc.restartRequests==1 && !s.pc.lookLocked;
    });
    check("cancel of a reopened native page preserves the earlier confirmed deployment", [](Scene& s){
        s.state.deaths=1; s.pc.StateName=L"Playing"; s.play(); EntryConfirm(&s.pc,L"PROBE"); s.tick(); s.tick();
        s.pc.OpenInGameSelectRoleMenu(); s.tick(); s.pc.CloseInGameSelectRoleMenu(); s.tick();
        if(s.pc.restartRequests) return false; s.pc.StateName=L"Inactive"; s.tick();
        s.possess(); s.settle(); return s.pc.restartRequests==1 && !s.pc.moveLocked;
    });
    check("role acknowledgement and the actually possessed role must both match", [](Scene& s){
        s.state.deaths=1; s.play(); EntryConfirm(&s.pc,L"PEACE");
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick(); s.possess(); s.state.PossessedCharacterId=L"PROBE";
        s.settle(); if(!s.pc.lookLocked) return false;
        s.state.PossessedCharacterId=L"PEACE"; s.settle(); return !s.pc.lookLocked && !s.pc.moveLocked;
    });
    check("same frame confirmation outside a running local PVE round remains stock", [](Scene& s){
        s.state.deaths=1; s.tick(); s.match.round=false;
        s.pc.OpenInGameSelectRoleMenu(); EntryConfirm(&s.pc,L"PROBE"); s.pc.CloseInGameSelectRoleMenu();
        s.tick(); s.possess(); s.settle(); return !s.pc.restartRequests && !s.pc.gameFocus && s.pc.gameLocked;
    });
    auto living = [](Scene& s){ s.state.deaths=2; s.state.life=EPBPlayerLifeStatus::Alive; s.tick(); };
    auto died = [](Scene& s){ ++s.state.deaths; s.state.life=EPBPlayerLifeStatus::Death; std::this_thread::sleep_for(32ms); };
    // 复用同一个原生提示：第二轮玩家取消真正的离场弹窗，第三轮立即换人。
    auto thirdAfterBack = [&](Scene& s, bool firstSample){
        s.leave.onBack=[&]{s.show(nullptr); s.pc.menu=false;};
        auto death=[&]{s.pc.StateName=L"Playing";s.pc.pawn=nullptr;died(s);};
        auto enter=[&]{s.key();s.pc.menu=true;s.show(&s.leave);s.tick();s.release();s.tick();s.tick();};
        auto deploy=[&](const wchar_t* role){EntryConfirm(&s.pc,role);s.state.SelectedCharacterID=role;s.tick();s.tick();};
        auto steady=[&]{std::this_thread::sleep_for(1230ms);s.tick();s.tick();std::this_thread::sleep_for(125ms);s.tick();};
        death();s.tick();enter();if(s.pc.openings!=1)return false;
        deploy(L"PEACE");s.possess();s.settle();steady();
        death();s.tick();enter();if(s.pc.openings!=2)return false;
        deploy(L"PROBE");
        s.key();s.pc.menu=true;s.show(&s.leave);s.tick();s.release();s.tick();s.possess();s.settle();
        if(s.prompts.current!=&s.leave || s.leave.back!=2)return false;
        s.leave.BP_OnHandleBackAction();s.settle();if(s.pc.bShowMouseCursor || s.pc.lookLocked)return false;
        steady();death();if(!firstSample){for(int i=0;i<16;++i)s.tick();}enter();
        return s.pc.openings==3 && s.leave.back==4 && !s.prompts.current;
    };
    check("fresh Escape on the first sampled death with an empty stack opens once", [&](Scene& s){
        living(s); died(s); s.key(); s.tick(); s.release(); s.tick(); s.tick(); return s.pc.openings==1 && !s.leave.back;
    });
    check("new Leave on first death sample still waits for native permission", [&](Scene& s){
        living(s); died(s); s.pc.selectable=false; s.key(); s.show(&s.leave); s.tick(); s.release(); s.tick();
        if(!s.untouched()) return false; s.pc.selectable=true; s.tick(); s.tick(); if(!s.fixed()) return false;
        {Scene sampled; if(!thirdAfterBack(sampled,false))return false;}
        Scene first; return thirdAfterBack(first,true);
    });
    check("Leave already shown in combat keeps focus on first death sample", [&](Scene& s){
        s.show(&s.leave); living(s); died(s); s.key(); s.tick(); s.release(); s.tick();
        return s.untouched() && s.prompts.current==&s.leave;
    });
    check("held combat key is not redeploy input after death", [&](Scene& s){
        s.pc.down={L"Escape"}; living(s); died(s); s.tick(); s.release(); s.tick(); return s.untouched();
    });
    check("first death input leaves unrelated visible modal alone", [&](Scene& s){
        living(s); died(s); s.key(); s.show(&s.otherPrompt); s.tick(); s.release(); s.tick(); return s.untouched();
    });
    check("hidden inactive owned previous option does not block next death", [&](Scene& s){
        living(s); died(s); s.menu.owner=&s.pc; s.menu.active=s.menu.visible=false;
        s.menu.typeName.path="InGameOption_V2_C"; s.inGame.current=&s.menu; s.play(); return s.fixed();
    });
    check("inactive but visible option and hidden foreign layer keep their stack", [&](Scene& s){
        living(s); died(s); s.menu.owner=&s.pc; s.menu.active=false; s.menu.visible=true;
        s.menu.typeName.path="InGameOption_V2_C"; s.inGame.current=&s.menu; s.play(); if(!s.untouched()) return false;
        s.menu.owner=&s.other; s.menu.visible=false; s.play(); return s.untouched();
    });
    check("spectator never turns a fresh menu press into deployment", [&](Scene& s){
        living(s); died(s); s.state.bOnlySpectator=true; s.play(); return s.untouched() && !s.pc.restartRequests;
    });
    check("match or round end rejects first death key even from a clean screen", [&](Scene& s){
        living(s); died(s); s.match.round=false; s.key(); s.show(&s.leave); s.tick(); s.release(); s.tick();
        return s.untouched() && !s.pc.restartRequests;
    });
    constexpr std::string_view boundEvent="Function UMG_Prompt.UMG_Prompt_C.CustomEvent_1";
    constexpr std::string_view nativeEvent="Function ProjectBoundary.PBPopUpWidget.OnOptionButtonClick";
    constexpr std::string_view unrelatedEvent="Function UMG_Other.UMG_Other_C.CustomEvent_1";
    auto pooledLeave = [&](Scene& s, int variant){
        s.state.deaths=2;s.pc.StateName=L"Playing";
        s.leave.onBack=[&]{s.show(nullptr);s.pc.menu=false;};
        s.play();EntryConfirm(&s.pc,L"PROBE");s.tick();s.tick();
        s.key();s.pc.menu=true;s.pc.bShowMouseCursor=true;s.show(&s.leave);s.tick();s.release();s.tick();s.possess();s.tick();
        if(variant!=6) {
            int index=variant==11?0:1;
            if(variant>=16)EntryPopupEvent(&s.leave,variant==17?nativeEvent:boundEvent,&index);
            else EntryCancel(&s.leave,index);
            s.leave.OnOptionButtonClick(1);
        } else s.leave.BP_OnHandleBackAction();
        s.pc.bShowMouseCursor=false;
        if(variant>=16)s.pc.gameLocked=s.pc.moveLocked=s.pc.lookLocked=false;
        s.tick();
        if(variant==5)s.settle();else std::this_thread::sleep_for(145ms);
        s.show(variant==2?&s.otherPrompt:&s.leave);
        if(variant==1)s.leave.TitleText=s.otherPrompt.TitleText;
        if(variant==3)s.pc.menu=s.pc.bShowMouseCursor=true;
        else s.pc.menu=s.pc.bShowMouseCursor=false;
        s.tick();if(variant<16)s.settle();
        if(variant==14){
            // 玩家生前主动用同一个控件重开真正的普通菜单，再正常关掉，不得借用原 Cancel。
            s.pc.menu=s.pc.bShowMouseCursor=true; s.tick();
            s.pc.menu=s.pc.bShowMouseCursor=false; s.tick();
        }
        s.state.life=EPBPlayerLifeStatus::Death;s.state.deaths+=(variant==4?2:1);s.pc.pawn=nullptr;
        if(variant==7)s.pc.selectable=false;
        if(variant==8)s.pc.down={L"Escape"};
        if(variant==9)s.driver.ServerConnection=&s.anotherConnection;
        if(variant==10)s.pc.PlayerState=&s.alternateState;
        if(variant==12)s.presented=&s.replacement;
        if(variant==13)s.leave.OptionTitleTexts[1].value=L"退出游戏";
        std::this_thread::sleep_for(32ms);
        if(variant>=18){
            if(variant==19)s.inputs.ActionMappings.clear();
            if(variant==20)s.pc.down={L"Escape"};
            if(variant!=22)s.tick();
            if(variant==19)s.inputs.ActionMappings.push_back({L"InGameMenu",{L"Escape"}});
            if(variant==23)std::this_thread::sleep_for(265ms);
            if(variant==24)++s.state.deaths;
            s.pc.down={L"Escape"};
        }else{if(variant<16){s.tick();s.tick();}if(variant!=8)s.key();}
        s.pc.menu=s.pc.bShowMouseCursor=variant!=21;s.tick();
        s.release();s.tick();s.tick();
        return variant==0||variant==5||variant==16||variant==17||variant==18 ? s.pc.openings==2 && s.leave.back==2 && !s.prompts.current :
            (s.pc.openings==0 && s.leave.back==(variant==6?1:0)) ||
            (s.pc.openings==1 && s.leave.back==(variant==6?2:1) && s.prompts.current && s.otherPrompt.back==0);
    };
    check("fresh death key reuses a verified previous Leave after native menu reopens", [&](Scene& s){return pooledLeave(s,0);});
    check("the verified stale Leave is recognized after focus already returned", [&](Scene& s){return pooledLeave(s,5);});
    check("pooled Leave reused for Quit stays open on a new key", [&](Scene& s){return pooledLeave(s,1);});
    check("a different prompt cannot borrow a cancelled Leave identity", [&](Scene& s){return pooledLeave(s,2);});
    check("a real open menu before a new death is never the stale widget exception", [&](Scene& s){
        if(!pooledLeave(s,3))return false; Scene reopened;return pooledLeave(reopened,14);
    });
    check("the stale Leave exception cannot skip an unobserved death", [&](Scene& s){return pooledLeave(s,4);});
    check("native Back alone does not authorize reuse of the pooled Leave", [&](Scene& s){return pooledLeave(s,6);});
    check("fresh stale widget key cannot open roles without native permission", [&](Scene& s){return pooledLeave(s,7);});
    check("held key cannot use the cancelled Leave exception", [&](Scene& s){return pooledLeave(s,8);});
    check("different connection cannot reuse a cancelled Leave", [&](Scene& s){return pooledLeave(s,9);});
    check("different player state cannot reuse a cancelled Leave", [&](Scene& s){return pooledLeave(s,10);});
    check("confirm option is not a Cancel receipt", [&](Scene& s){return pooledLeave(s,11);});
    check("different world cannot reuse a cancelled Leave", [&](Scene& s){return pooledLeave(s,12);});
    check("same prompt with different buttons cannot reuse a cancelled Leave", [&](Scene& s){return pooledLeave(s,13);});
    check("actual bound Cancel and natural control prime the very first next death key", [&](Scene& s){return pooledLeave(s,16);});
    check("working control behind a returned native Leave does not keep deployment pending", [&](Scene& s){return pooledLeave(s,17);});
    check("a sampled dead release permits the native down-only rise to reopen only the verified Leave", [&](Scene& s){return pooledLeave(s,18);});
    check("down-only reuse requires a real released configured key and a subsequent menu opening", [&](Scene& s){if(!pooledLeave(s,19))return false;Scene a;if(!pooledLeave(a,20))return false;Scene b;return pooledLeave(b,21);});
    check("down-only reuse cannot originate in combat or cross an old or different death sample", [&](Scene& s){if(!pooledLeave(s,22))return false;Scene a;if(!pooledLeave(a,23))return false;Scene b;return pooledLeave(b,24);});
    // 实机：额外 Esc 的原生提示跨过出生，真正 Cancel 后仅客户端丢失刚才确实占有过的角色。
    auto lostAfterCancel = [&](Scene& s, int variant){
        s.state.deaths=2; s.pc.StateName=L"Playing";
        s.leave.onBack=[&]{s.show(nullptr);s.pc.menu=false;};
        s.play();
        if(variant!=15)EntryConfirm(&s.pc,L"PEACE");
        s.state.SelectedCharacterID=L"PEACE"; s.tick(); s.tick();
        s.key(); s.pc.menu=s.pc.bShowMouseCursor=true; s.show(&s.leave); s.tick(); s.release(); s.tick();
        s.possess(); s.pc.StateName=L"InGameMenu"; s.tick();
        if(variant==12)s.newPawn.owner=&s.other;
        if(variant==13)s.state.PawnPrivate=nullptr;
        const int option=variant==1||variant==41?0:1;
        if(variant>=29) {
            if(variant==40)s.leave.owner=&s.other;
            EntryPopupEvent(&s.leave,variant==32?unrelatedEvent:boundEvent,variant==42?nullptr:&option);
            if(variant==31)EntryPopupEvent(&s.leave,nativeEvent,&option);
        } else if(variant!=2)EntryCancel(&s.leave,option);
        if(variant==2)s.leave.BP_OnHandleBackAction();else s.leave.OnOptionButtonClick(1);
        s.pc.menu=s.pc.bShowMouseCursor=false; s.pc.StateName=L"Inactive"; s.pc.pawn=nullptr; s.pc.view=&s.pc;
        s.newPawn.owner=nullptr;s.newPawn.firstPerson=false;s.state.PawnPrivate=&s.newPawn;
        APBCharacter otherPawn;
        if(variant==3)s.state.PawnPrivate=&otherPawn;
        if(variant==4)s.newPawn.alive=false;
        if(variant==5)s.state.life=EPBPlayerLifeStatus::Downed;
        if(variant==6)s.newPawn.owner=&s.other;
        if(variant==7)s.state.bIsSpectator=true;
        if(variant==8)s.state.PossessedCharacterId=L"PROBE";
        if(variant==9)++s.state.deaths;
        if(variant==10)s.driver.ServerConnection=&s.anotherConnection;
        if(variant==11)s.match.round=false;
        if(variant==14)s.pc.view=&otherPawn;
        if(variant==16)s.pc.AcknowledgedPawn=&otherPawn;
        if(variant==17)s.presented=&s.replacement;
        if(variant==18)s.pc.PlayerState=&s.alternateState;
        if(variant==19)s.newPawn.PlayerState=&s.alternateState;
        if(variant==20)s.pc.StateName=L"Spectating";
        if(variant==21)s.pc.pawn=&otherPawn;
        if(variant==37)s.show(&s.leave);
        const int restarts=s.pc.restartRequests,focus=s.pc.gameFocus;
        s.tick(); if(s.pc.possessionChecks)return false;
        if(variant==22){s.possess();s.newPawn.firstPerson=true;s.pc.bShowMouseCursor=false;}
        if(variant==23){s.show(&s.otherPrompt);s.pc.menu=s.pc.bShowMouseCursor=true;}
        if(variant==24){s.menu.owner=&s.pc;s.menu.typeName.path="InGameOption_V2_C";s.inGame.current=&s.menu;}
        if(variant==25)s.pc.bShowMouseCursor=true;
        if(variant==26)s.state.bOnlySpectator=true;
        if(variant==27)s.pc.local=false;
        if(variant==28)s.state.SelectedCharacterID=L"PROBE";
        const bool returned=variant==30||variant==31||(variant>=33&&variant<=39);
        if(returned) {
            s.show(variant==34?&s.otherPrompt:&s.leave);
            if(variant==33)s.leave.TitleText=s.otherPrompt.TitleText;
            if(variant==35)s.leave.OptionTitleTexts[1].value=L"Quit";
            if(variant==36)s.key();
            if(variant==38)s.pc.menu=s.pc.bShowMouseCursor=true;
            if(variant==39)s.leave.visible=false;
            s.tick();
            if(variant==36)s.release();
            if(variant==38)s.pc.menu=s.pc.bShowMouseCursor=false;
        }
        std::this_thread::sleep_for(525ms);s.tick();s.tick();
        const bool shouldCheck=variant==0||variant==29||variant==30||variant==31;
        if(shouldCheck){
            if(s.pc.possessionChecks!=1||s.pc.pawn||s.pc.gameFocus!=focus||s.pc.StateName!=L"Inactive")return false;
            std::this_thread::sleep_for(525ms);s.tick();
        }
        return s.pc.possessionChecks==(shouldCheck?1:0) && s.pc.restartRequests==restarts &&
            (!returned||s.prompts.current==(variant==34?&s.otherPrompt:&s.leave)) &&
            (variant!=23||s.prompts.current==&s.otherPrompt);
    };
    check("cancel after birth asks the server once about the still living previously owned pawn", [&](Scene& s){return lostAfterCancel(s,0);});
    check("confirm or Back never authorizes a possession check", [&](Scene& s){if(!lostAfterCancel(s,1))return false;Scene other;return lostAfterCancel(other,2);});
    check("a different state pawn or dead character never triggers a possession check", [&](Scene& s){if(!lostAfterCancel(s,3))return false;Scene other;return lostAfterCancel(other,4);});
    check("downed and spectator players keep stock possession", [&](Scene& s){if(!lostAfterCancel(s,5))return false;Scene a;if(!lostAfterCancel(a,7))return false;Scene b;return lostAfterCancel(b,26);});
    check("another owner or player state cannot authorize resynchronization", [&](Scene& s){if(!lostAfterCancel(s,6))return false;Scene other;return lostAfterCancel(other,19);});
    check("a changed role or later death invalidates the observed birth", [&](Scene& s){if(!lostAfterCancel(s,8))return false;Scene a;if(!lostAfterCancel(a,9))return false;Scene b;return lostAfterCancel(b,28);});
    check("connection world and player identity changes do not send a possession check", [&](Scene& s){if(!lostAfterCancel(s,10))return false;Scene a;if(!lostAfterCancel(a,17))return false;Scene b;return lostAfterCancel(b,18);});
    check("round end or a nonlocal controller cannot reconcile possession", [&](Scene& s){if(!lostAfterCancel(s,11))return false;Scene a;return lostAfterCancel(a,27);});
    check("a birth not witnessed on the owning controller and player state cannot be restored", [&](Scene& s){if(!lostAfterCancel(s,12))return false;Scene a;return lostAfterCancel(a,13);});
    check("a spectator view or acknowledged other pawn rules out the cancel race", [&](Scene& s){if(!lostAfterCancel(s,14))return false;Scene a;if(!lostAfterCancel(a,16))return false;Scene b;return lostAfterCancel(b,20);});
    check("an unconfirmed selection never requests possession", [&](Scene& s){return lostAfterCancel(s,15);});
    check("another or spontaneously restored pawn remains untouched", [&](Scene& s){if(!lostAfterCancel(s,21))return false;Scene a;return lostAfterCancel(a,22);});
    check("a visible prompt option layer or cursor suppresses a pending possession check", [&](Scene& s){if(!lostAfterCancel(s,23))return false;Scene a;if(!lostAfterCancel(a,24))return false;Scene b;return lostAfterCancel(b,25);});
    check("bound stock Cancel callback reaches the possession check without a separate native event", [&](Scene& s){return lostAfterCancel(s,29);});
    check("the same returned Leave retains its widget while the server checks the missing pawn", [&](Scene& s){return lostAfterCancel(s,30);});
    check("nested bound and native Cancel still produce only one request", [&](Scene& s){return lostAfterCancel(s,31);});
    check("unrelated delegates wrong options empty parameters and foreign prompts stay stock", [&](Scene& s){if(!lostAfterCancel(s,32))return false;Scene a;if(!lostAfterCancel(a,40))return false;Scene b;if(!lostAfterCancel(b,41))return false;Scene c;return lostAfterCancel(c,42);});
    check("a returned prompt with changed buttons title or owner is not passive", [&](Scene& s){if(!lostAfterCancel(s,33))return false;Scene a;if(!lostAfterCancel(a,34))return false;Scene b;return lostAfterCancel(b,35);});
    check("a new press or a newly opened living menu cancels possession reconciliation", [&](Scene& s){if(!lostAfterCancel(s,36))return false;Scene a;return lostAfterCancel(a,38);});
    check("the prompt must visibly leave the stack before any return and its identity stays visible", [&](Scene& s){if(!lostAfterCancel(s,37))return false;Scene a;return lostAfterCancel(a,39);});
    // 实机第 3 死：选回同干员，确认确已到达；旧 Leave 在开页后又退栈、复入。第 4 死仍要有独立的新键。
    auto repeatBehindLeave = [&](Scene& s,int variant){
        const int cancel=1;const bool positive=variant<=1||variant==8||variant==23||variant==24;
        s.state.deaths=2;s.pc.StateName=L"Playing";s.leave.onBack=[&]{s.show(nullptr);s.pc.menu=false;};
        s.play();EntryConfirm(&s.pc,L"PEACE");s.state.SelectedCharacterID=L"PEACE";s.tick();s.tick();
        s.key();s.pc.menu=s.pc.bShowMouseCursor=true;s.show(&s.leave);s.tick();s.release();s.tick();s.possess();s.tick();
        if(variant!=2)EntryPopupEvent(&s.leave,boundEvent,&cancel);
        s.leave.OnOptionButtonClick(1);s.pc.bShowMouseCursor=false;s.pc.gameLocked=s.pc.moveLocked=s.pc.lookLocked=false;s.tick();
        std::this_thread::sleep_for(145ms);s.show(&s.leave);s.tick();std::this_thread::sleep_for(35ms);
        ++s.state.deaths;s.state.life=EPBPlayerLifeStatus::Death;s.pc.pawn=nullptr;s.newPawn.alive=false;
        s.tick();s.key();s.pc.menu=true;s.tick();s.release();s.tick();s.tick();
        if(!s.pc.bShowSelectRole||s.pc.openings!=2)return !positive;
        s.pc.bShowMouseCursor=false;
        if(variant==3)s.leave.onBack=[&]{s.pc.menu=false;};
        s.show(&s.leave);s.tick();s.tick(); // 原生已处理开页的旧 Leave，除 variant 3 外本轮先看到空栈。
        s.show(variant==4?&s.otherPrompt:&s.leave);s.tick();
        const int focus=s.pc.gameFocus,closed=s.pc.closings,back=s.leave.back,opens=s.pc.openings,checks=s.pc.possessionChecks;
        const wchar_t* requested=variant==8||variant>=25?L"SPIKE":L"PEACE";
        if(variant!=5&&variant!=27)EntryConfirm(&s.pc,requested);
        s.state.SelectedCharacterID=requested;s.tick();
        if(variant==6){s.key();s.tick();s.release();}
        if(variant==7)s.leave.OptionTitleTexts[1].value=L"QUIT";
        if(variant==9){s.pc.menu=true;s.tick();s.pc.menu=false;}
        if(variant==10){s.pc.bShowMouseCursor=true;s.tick();s.pc.bShowMouseCursor=false;}
        if(variant==11)s.leave.owner=&s.other;
        if(variant==12)s.leave.visible=false;
        s.possess();s.newPawn.alive=true;s.pc.bShowMouseCursor=false;
        s.pc.gameLocked=s.pc.moveLocked=s.pc.lookLocked=variant==1||variant==13;
        if(variant==14)s.pc.view=&s.other;
        if(variant==15)s.newPawn.owner=&s.other;
        if(variant==16)s.state.PossessedCharacterId=L"SPIKE";
        if(variant==17){s.menu.owner=&s.pc;s.menu.typeName.path="InGameOption_V2_C";s.inGame.current=&s.menu;}
        if(variant==18)s.state.bIsSpectator=true;
        if(variant==19)s.driver.ServerConnection=&s.anotherConnection;
        if(variant==20)s.presented=&s.replacement;
        if(variant==21)s.pc.selectable=false;
        if(variant==22)s.pc.PlayerState=&s.alternateState;
        if(variant==25)s.state.PossessedCharacterId=L"PEACE";
        if(variant==26){s.state.SelectedCharacterID=L"PROBE";s.state.PossessedCharacterId=L"PROBE";}
        s.tick();s.tick();
        if(variant==1){
            if(!s.pc.bShowSelectRole||s.pc.closings!=closed||!s.pc.lookLocked||s.pc.gameFocus!=focus)return false;
            s.pc.gameLocked=s.pc.moveLocked=s.pc.lookLocked=false;s.tick();
        }
        if(!positive)return s.pc.closings==closed&&s.pc.bShowSelectRole&&s.leave.back==back&&s.pc.gameFocus==focus&&s.pc.possessionChecks==checks;
        if(s.pc.closings!=closed+1||s.pc.bShowSelectRole||s.prompts.current!=&s.leave||s.leave.back!=back||s.pc.gameFocus!=focus||s.pc.possessionChecks!=checks)return false;
        if(variant==23){s.show(&s.otherPrompt);s.pc.menu=s.pc.bShowMouseCursor=true;}
        s.tick();std::this_thread::sleep_for(35ms);
        ++s.state.deaths;s.state.life=EPBPlayerLifeStatus::Death;s.pc.pawn=nullptr;s.newPawn.alive=false;
        s.tick();
        if(variant==24){s.pc.down={L"Escape"};s.tick();}
        else s.key();
        s.pc.menu=s.pc.bShowMouseCursor=true;s.tick();s.release();s.tick();s.tick();
        if(variant==23||variant==24)return s.pc.openings==opens&&s.prompts.current==(variant==23?&s.otherPrompt:&s.leave);
        return s.pc.openings==opens+1&&s.pc.closings==closed+1&&s.leave.back==back+1&&s.pc.possessionChecks==checks;
    };
    check("confirmed repeat behind the twice returned original Leave closes once and permits a fourth death key", [&](Scene& s){return repeatBehindLeave(s,0);});
    check("locked repeat waits for real local control before retiring its page without taking focus", [&](Scene& s){return repeatBehindLeave(s,1);});
    check("a card alone without deployment or an unverified previous Cancel cannot retire the role page", [&](Scene& s){if(!repeatBehindLeave(s,5))return false;Scene a;return repeatBehindLeave(a,2);});
    check("the opening Leave must actually leave the stack and reappear as the same scoped widget", [&](Scene& s){if(!repeatBehindLeave(s,3))return false;Scene a;return repeatBehindLeave(a,4);});
    check("a new menu gesture or new regular menu invalidates repeat-close eligibility", [&](Scene& s){if(!repeatBehindLeave(s,6))return false;Scene a;if(!repeatBehindLeave(a,9))return false;Scene b;return repeatBehindLeave(b,10);});
    check("modified Leave buttons foreign ownership or invisibility never allow repeat close", [&](Scene& s){if(!repeatBehindLeave(s,7))return false;Scene a;if(!repeatBehindLeave(a,11))return false;Scene b;return repeatBehindLeave(b,12);});
    check("a mismatched possessed role keeps the original prompt in charge", [&](Scene& s){return repeatBehindLeave(s,16);});
    check("locked input foreign view or foreign pawn always keeps repeat page and input intact", [&](Scene& s){if(!repeatBehindLeave(s,13))return false;Scene a;if(!repeatBehindLeave(a,14))return false;Scene b;return repeatBehindLeave(b,15);});
    check("visible options spectators and revoked native permission do not close the repeat page", [&](Scene& s){if(!repeatBehindLeave(s,17))return false;Scene a;if(!repeatBehindLeave(a,18))return false;Scene b;return repeatBehindLeave(b,21);});
    check("replacement connection world or player state cannot inherit repeat-close authority", [&](Scene& s){if(!repeatBehindLeave(s,19))return false;Scene a;if(!repeatBehindLeave(a,20))return false;Scene b;return repeatBehindLeave(b,22);});
    check("a different next prompt or a held fourth death key keeps the original UI after successful close", [&](Scene& s){if(!repeatBehindLeave(s,23))return false;Scene a;return repeatBehindLeave(a,24);});
    check("confirmed different role behind the same returned Leave closes for its real new pawn and preserves the next death key", [&](Scene& s){return repeatBehindLeave(s,8);});
    check("a different unconfirmed card or a later selected and possessed other role cannot close the deployment page", [&](Scene& s){if(!repeatBehindLeave(s,25))return false;Scene a;if(!repeatBehindLeave(a,26))return false;Scene b;return repeatBehindLeave(b,27);});
#endif
#ifdef TEST_V084
    check("release reconnect delivers the same local session", [](Scene& s){ EntryReconnect(&s.origin,&s.local,"127.0.0.1:7908"); s.play(); return s.fixed(); });
    check("other character ticks after local pawn is gone", [](Scene& s){ s.play(); return s.fixed(); });
    check("late remote return cannot overwrite latest travel", [](Scene& s){ EntryReordered(&s.origin,&s.local); s.play(); return s.untouched(); });
    check("simultaneous local players fail closed", [](Scene& s){ s.instance.LocalPlayers.push_back(&s.local); s.play(); return s.untouched(); });
#endif
    return failed ? 1 : 0;
}

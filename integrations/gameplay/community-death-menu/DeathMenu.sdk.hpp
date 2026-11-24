#pragma once

#include "DeathMenu.hpp"

namespace BrtDeathMenu
{
inline Room room;
inline Gesture gesture;
inline Time nextSample{};
inline thread_local bool reading = false;

struct OpenSelection
{
    SDK::APBPlayerController* owner{};
    SDK::APBPlayerState* state{};
    SDK::FName initialRole{}, requestedRole{};
    bool confirmed{}, closing{};
    SDK::UCommonActivatableWidget* escapePrompt{};
    Time promptGrace{}, confirmUntil{};
};
inline OpenSelection selection;

// 关闭选角页并不等于新角色已能接收输入。只跟踪这一次部署及其本机所有者。
struct InputReturn {
    SDK::APBPlayerController* owner{};
    SDK::APBPlayerState* state{};
    SDK::APBCharacter* pawn{};
    Time expires{}, stable{};
    bool applied{}, retried{};
    SDK::FName restartRole{};
    int deaths{};
};
inline InputReturn inputReturn;

inline void RequestPendingRestart(SDK::APBPlayerController* pc, SDK::APBPlayerState* state)
{
    using namespace SDK;
    auto& role = inputReturn.restartRole;
    if (role == FName{}) return;
    if (!(role == state->SelectedCharacterID) || state->GetDeaths() != inputReturn.deaths ||
        !state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death)) {
        role = {};
        return;
    }
    // 客户端只提交一次；专服在自己确认同一次部署仍未出生后处理剩余等待。
    static const auto inactive = UKismetStringLibrary::Conv_StringToName(L"Inactive");
    if (pc->StateName == inactive) {
        role = {};
        pc->ServerRestartPlayer();
    }
}

inline void OpenRoles(SDK::APBPlayerController* pc, SDK::APBPlayerState* state)
{
    selection = {pc, state, state->SelectedCharacterID};
    selection.escapePrompt = static_cast<SDK::UCommonActivatableWidget*>(const_cast<void*>(gesture.closing));
    selection.promptGrace = Clock::now() + std::chrono::milliseconds(900);
    pc->OpenInGameSelectRoleMenu();
}

inline void Confirmed(SDK::UObject* sender, SDK::FName role)
{
    if (selection.owner && sender == selection.owner && !(role == SDK::FName{}))
    {
        selection.requestedRole = role;
        selection.confirmed = true;
        selection.confirmUntil = Clock::now() + std::chrono::seconds(20);
    }
}

inline Prompt Classify(SDK::UCommonActivatableWidget*, SDK::APBPlayerController*);

inline void FinishInputReturn(SDK::APBPlayerController* pc, SDK::APBPlayerState* state,
    SDK::UCommonActivatableWidget* prompt, SDK::UCommonActivatableWidget* menu,
    SDK::UCommonActivatableWidget* inGame, Time now)
{
    using namespace SDK;
    auto& r = inputReturn;
    if (r.owner != pc || r.state != state || now >= r.expires || prompt || pc->IsInGameMenuOpened() ||
        pc->bShowSelectRole || (menu && menu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGame && (inGame->GetOwningPlayer() != pc || inGame->Class->GetName() != "InGameOption_V2_C"))) {
        r = {};
        return;
    }
    RequestPendingRestart(pc, state);
    auto* pawn = pc->GetPBCharacter();
    if (!state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive) ||
        !pawn || pawn != pc->K2_GetPawn() || pawn->GetController() != pc ||
        !pawn->IsAlive() || pawn->IsInKilledStatus() || !pawn->IsFirstPerson()) {
        if (r.pawn) r = {}; // 已离开刚才接管的角色，不继承到下一条命。
        return;
    }
    if (r.pawn && r.pawn != pawn) { r = {}; return; }
    if (!r.pawn) { r.pawn = pawn; r.stable = now; }
    if (inGame && inGame->IsActivated()) {
        inGame->DeactivateWidget();
        r.stable = now;
        return;
    }
    const auto age = now - r.stable;
    if (age < std::chrono::milliseconds(180)) return;
    const bool game = pc->IsGameInputIgnored(), move = pc->IsMoveInputIgnored(), look = pc->IsLookInputIgnored();
    if (!r.applied || (!r.retried && (game || move || look || pc->bShowMouseCursor))) {
        if (game) pc->ResetIgnoreGameInput();
        if (move) pc->ResetIgnoreMoveInput();
        if (look) pc->ResetIgnoreLookInput();
        pc->bShowMouseCursor = false;
        UWidgetBlueprintLibrary::SetInputMode_GameOnly(pc);
        UWidgetBlueprintLibrary::SetFocusToGameViewport();
        r.retried = r.applied;
        r.applied = true;
    }
    if (r.retried || age >= std::chrono::milliseconds(1200)) r = {};
}

inline void ReturnFromSelection(SDK::APBPlayerController* pc, SDK::APBPlayerState* state,
    SDK::UCommonActivatableWidget* prompt, SDK::UCommonActivatableWidget* menu,
    SDK::UCommonActivatableWidget* inGame)
{
    using namespace SDK;
    if (selection.owner != pc || selection.state != state)
    {
        selection = {};
        return;
    }
    if (prompt && prompt == selection.escapePrompt && Clock::now() <= selection.promptGrace &&
        Classify(prompt, pc) == Prompt::Leave)
    {
        selection.escapePrompt = nullptr;
        static_cast<UUMG_Prompt_C*>(prompt)->OnOptionButtonClick(1);
        return;
    }
    if (prompt || pc->IsInGameMenuOpened() ||
        (menu && menu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGame && (inGame->GetOwningPlayer() != pc || inGame->Class->GetName() != "InGameOption_V2_C")))
        return;
    const bool accepted = !(state->SelectedCharacterID == FName{}) &&
        (selection.confirmed ? state->SelectedCharacterID == selection.requestedRole :
                               !(state->SelectedCharacterID == selection.initialRole));
    if (pc->bShowSelectRole)
    {
        if (selection.closing || !accepted)
            return;
        // 等原生角色选择得到确认后才走正常的关闭流程，下一次采样再读菜单栈。
        selection.closing = true;
        pc->CloseInGameSelectRoleMenu();
        return;
    }
    // 原生控件可先关页、稍后才同步新干员 ID。不能把已确认的部署当作取消。
    if (selection.confirmed && !accepted) {
        if (Clock::now() < selection.confirmUntil) return;
        selection.confirmed = false;
    }
    if (inGame && inGame->IsActivated())
    {
        inGame->DeactivateWidget();
        return;
    }
    if (accepted) {
        inputReturn = {pc, state, nullptr, Clock::now() + std::chrono::seconds(35)};
        if (selection.confirmed && state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death)) {
            inputReturn.restartRole = selection.requestedRole;
            inputReturn.deaths = state->GetDeaths();
        }
    }
    selection = {};
    pc->bShowMouseCursor = false;
    UWidgetBlueprintLibrary::SetInputMode_GameOnly(pc);
    if (accepted) RequestPendingRestart(pc, state);
}

struct ReadingScope
{
    ReadingScope() { reading = true; }
    ~ReadingScope() { reading = false; }
};

inline void Travel(SDK::UWorld* world, SDK::UPBLocalPlayer* local, std::string_view target)
{
    ReadingScope scope;
    const bool selected = SDK::UKismetSystemLibrary::ParseParam(
        SDK::UKismetSystemLibrary::GetCommandLine(), L"BrtLocalPveUi");
    room.Travel(selected, target, world, world->OwningGameInstance, local, Clock::now());
    gesture = {};
    selection = {};
    inputReturn = {};
    nextSample = {};
}

inline bool TextMatches(const SDK::FText& value, const wchar_t* nameSpace, const wchar_t* key)
{
    SDK::FText localized{};
    return SDK::UKismetTextLibrary::FindTextInLocalizationTable(nameSpace, key, &localized) &&
        SDK::UKismetTextLibrary::EqualEqual_TextText(value, localized);
}

inline bool MatchesPopupText(const SDK::FText& value, const wchar_t* nativeKey, const wchar_t* tableKey)
{
    return TextMatches(value, L"PopUpPage", nativeKey) || TextMatches(value, L"PopUp", tableKey);
}

inline Prompt Classify(SDK::UCommonActivatableWidget* top, SDK::APBPlayerController* pc)
{
    if (!top)
        return Prompt::None;
    if (!top->IsA(SDK::UUMG_Prompt_C::StaticClass()) || top->GetOwningPlayer() != pc)
        return Prompt::Other;
    auto* popup = static_cast<SDK::UUMG_Prompt_C*>(top);
    if (!popup->IsActivated() || SDK::UKismetTextLibrary::TextIsEmpty(popup->TitleText))
        return Prompt::Initializing;
    if (!MatchesPopupText(popup->TitleText, L"LEAVE MATCH", L"Other_PopUp_LeaveMatch.Button"))
        return Prompt::Other;
    if (SDK::UKismetTextLibrary::TextIsEmpty(popup->DescriptionText) || popup->OptionTitleTexts.Num() < 2)
        return Prompt::Initializing;
    if (popup->OptionTitleTexts.Num() != 2 ||
        !(MatchesPopupText(popup->DescriptionText, L"PLEASE CONFIRM YOUR OPERATION", L"Other_PopUp_ConfirmOperation.Title") ||
          TextMatches(popup->DescriptionText, L"PopUpPage", L"PLEASE CONFIRM YOUR COMMAND")) ||
        !MatchesPopupText(popup->OptionTitleTexts[0], L"CONFIRM", L"Other_PopUp_Confirm.Title") ||
        !MatchesPopupText(popup->OptionTitleTexts[1], L"CANCEL", L"Other_PopUp_Cancel.Title"))
        return Prompt::Other;
    return Prompt::Leave;
}

inline SDK::FKey NamedKey(const wchar_t* name)
{
    SDK::FKey key{};
    key.KeyName = SDK::UKismetStringLibrary::Conv_StringToName(name);
    return key;
}

inline bool ModifiersDown(SDK::APBPlayerController* pc, const SDK::FInputActionKeyMapping& map)
{
    static const auto shiftL = NamedKey(L"LeftShift"), shiftR = NamedKey(L"RightShift");
    static const auto ctrlL = NamedKey(L"LeftControl"), ctrlR = NamedKey(L"RightControl");
    static const auto altL = NamedKey(L"LeftAlt"), altR = NamedKey(L"RightAlt");
    static const auto cmdL = NamedKey(L"LeftCommand"), cmdR = NamedKey(L"RightCommand");
    return (!map.bShift || pc->IsInputKeyDown(shiftL) || pc->IsInputKeyDown(shiftR)) &&
        (!map.bCtrl || pc->IsInputKeyDown(ctrlL) || pc->IsInputKeyDown(ctrlR)) &&
        (!map.bAlt || pc->IsInputKeyDown(altL) || pc->IsInputKeyDown(altR)) &&
        (!map.bCmd || pc->IsInputKeyDown(cmdL) || pc->IsInputKeyDown(cmdR));
}

inline void ReadAction(SDK::APBPlayerController* pc, Frame& f)
{
    static const auto name = SDK::UKismetStringLibrary::Conv_StringToName(L"InGameMenu");
    auto* settings = SDK::UInputSettings::GetInputSettings();
    auto* options = static_cast<SDK::UPBOptions*>(
        SDK::USubsystemBlueprintLibrary::GetEngineSubsystem(SDK::UPBOptions::StaticClass()));
    if (!settings || !options)
        return;
    // 只检查本作当前设备的实际按键。原版未列入可重绑定菜单的动作从引擎当前动作表读取。
    auto keys = options->GetCurrentDeviceKeysOfInput(name, 1.0f);
    if (keys.Num() == 0) keys = options->GetKeysFromActionName(name);
    auto add = [&](const SDK::FKey& key)
    {
        const bool pressed = pc->WasInputKeyJustPressed(key);
        const bool down = pc->IsInputKeyDown(key);
        if (!pressed && !down)
            return;
        bool mapped = false;
        bool satisfied = false;
        for (int i = 0; i < settings->ActionMappings.Num(); ++i)
        {
            const auto& row = settings->ActionMappings[i];
            if (!(row.ActionName == name && row.Key.KeyName == key.KeyName))
                continue;
            mapped = true;
            satisfied = satisfied || ModifiersDown(pc, row);
        }
        if (satisfied || !mapped)
        {
            f.pressed |= pressed;
            f.down |= down;
        }
    };
    if (keys.Num() > 0)
    {
        for (int i = 0; i < keys.Num(); ++i)
            add(keys[i]);
        return;
    }
    for (int i = 0; i < settings->ActionMappings.Num(); ++i)
    {
        const auto& row = settings->ActionMappings[i];
        if (row.ActionName == name)
            add(row.Key);
    }
}

inline bool Sample(SDK::UWorld* world, SDK::UPBLocalPlayer* local, Time now)
{
    using namespace SDK;
    auto* server = world->NetDriver ? world->NetDriver->ServerConnection : nullptr;
    if (!room.Check(world, world->OwningGameInstance, local, server, now))
        return false;
    auto* controller = local->PlayerController;
    if (!controller || !controller->IsA(APBPlayerController::StaticClass()) ||
        controller->Player != local || !controller->IsLocalPlayerController() ||
        (server->OwningActor != controller && server->PlayerController != controller))
        return false;
    auto* pc = static_cast<APBPlayerController*>(controller);
    auto* base = world->GameState;
    if (!base || !base->IsA(APBGameState::StaticClass()) || !base->GameModeClass.Get())
        return false;
    auto* gs = static_cast<APBGameState*>(base);
    if (!room.playWorld)
    {
        if (!IsPveClass(UKismetSystemLibrary::GetPathName(gs->GameModeClass.Get()).ToString()))
        {
            room = {};
            return false;
        }
        room.Bind(world, server, gs->GameModeClass.Get());
    }
    if (room.modeClass != gs->GameModeClass.Get())
    {
        room = {};
        return false;
    }
    if (!pc->PlayerState || !pc->PlayerState->IsA(APBPlayerState::StaticClass()))
        return false;
    auto* state = static_cast<APBPlayerState*>(pc->PlayerState);
    if (inputReturn.owner && (inputReturn.owner != pc || inputReturn.state != state || now >= inputReturn.expires))
        inputReturn = {};
    const bool dead = state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death);
    if (!selection.owner && !inputReturn.owner && !dead)
    {
        nextSample = now + std::chrono::milliseconds(30);
        return false;
    }
    if (!gs->HasMatchStarted() || gs->HasMatchEnded() || !gs->IsRoundInProgress())
        return false;
    if (!pc->PlayerInput) return selection.owner == pc || inputReturn.owner == pc;
    auto* manager = static_cast<UPBMainMenuManager_BP_C*>(
        USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController(pc, UPBMainMenuManager_BP_C::StaticClass()));
    auto* layout = manager ? manager->MainMenuLayout : nullptr;
    if (!layout || !layout->PromptStack || !layout->MenuStack || !layout->InGameMenuStack)
        return selection.owner == pc || inputReturn.owner == pc;
    auto* layoutOwner = layout->GetOwningPlayer();
    if (layoutOwner && layoutOwner != pc)
        return false;

    UCommonActivatableWidget *top{}, *mainMenu{}, *inGameMenu{};
    manager->GetTopPromptWidget(&top);
    manager->GetTopMenuWidget(&mainMenu);
    manager->GetTopInGameWidget(&inGameMenu);
    // 原生流程也能先于 Esc 观察器打开选角。首命不接管，真正死亡后的本机选角才跟踪。
    if (!selection.owner && pc->bShowSelectRole && dead && state->GetDeaths() > 0) {
        inputReturn = {};
        selection = {pc, state, state->SelectedCharacterID};
    }
    if (selection.owner)
    {
        ReturnFromSelection(pc, state, top, mainMenu, inGameMenu);
        return true;
    }
    if (inputReturn.owner) {
        FinishInputReturn(pc, state, top, mainMenu, inGameMenu, now);
        if (!dead) return true;
    }
    if (pc->bShowSelectRole) return false;
    Frame frame{};
    frame.controller = pc;
    frame.state = pc->PlayerState;
    frame.layout = layout;
    frame.prompt = top;
    // Guardian 是切图后仍在栈底的大厅占位控件。
    frame.menuInFront = (mainMenu && mainMenu->Class->GetName() != "UMG_MainMenuGuardian_C") || inGameMenu;
    frame.regularMenuOpen = pc->IsInGameMenuOpened();
    frame.canSelect = pc->CanSelectRole();
    ReadAction(pc, frame);
    if (top)
    {
        const bool needsText = gesture.phase != Gesture::Phase::Idle ||
            (gesture.previousClean && (frame.regularMenuOpen ||
                ((frame.pressed || frame.down) && !gesture.edgeConsumed)));
        frame.kind = needsText ? Classify(top, pc) : Prompt::Other;
    }
    const auto action = gesture.Observe(frame, now);
    // 同一按键的原生处理可能在 Back 返回后再次将同一个离场提示放回栈顶。
    const bool returnedLeave = gesture.phase == Gesture::Phase::WaitingClose &&
        frame.canSelect && frame.regularMenuOpen && !frame.pressed && !frame.down &&
        frame.kind == Prompt::Leave && top == gesture.closing;
    if (returnedLeave)
    {
        // 同个弹窗在按键松开后再次出现。取消结果经过原生按钮回调，
        // 等待 CommonUI 真正弹出它，随后才允许选角页取得焦点。
        static_cast<UUMG_Prompt_C*>(top)->OnOptionButtonClick(1);
        if (pc->IsInGameMenuOpened()) pc->CloseExitMatchInGameMenu();
    }
    else if (action == Action::Back)
    {
        static_cast<UUMG_Prompt_C*>(top)->BP_OnHandleBackAction();
        if (frame.regularMenuOpen)
            pc->CloseExitMatchInGameMenu();
    }
    else if (action == Action::OpenRoles)
        OpenRoles(pc, state);
    return true;
}

inline void Tick(SDK::UWorld* world, SDK::UPBLocalPlayer* local)
{
    if (!room.origin || reading)
        return;
    const Time now = Clock::now();
    if (now < nextSample)
        return;
    ReadingScope scope;
    nextSample = now + std::chrono::milliseconds(6);
    if (!Sample(world, local, now))
    {
        gesture = {};
        selection = {};
        inputReturn = {};
    }
}
}

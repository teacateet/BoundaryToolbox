#pragma once

#include "DeathMenu.hpp"

namespace BrtDeathMenu
{
inline Room room;
inline Gesture gesture;
inline Time nextSample{};
inline Time nextAliveUi{};
inline bool respawnKeyHeld{};
inline thread_local bool reading = false;

struct ReadingScope
{
    bool previous = reading;
    ReadingScope() { reading = true; }
    ~ReadingScope() { reading = previous; }
};

struct OpenSelection
{
    SDK::APBPlayerController* owner{};
    SDK::APBPlayerState* state{};
    SDK::FName initialRole{}, requestedRole{};
    bool confirmed{}, closing{};
    SDK::UCommonActivatableWidget* escapePrompt{};
    Time promptGrace{}, confirmUntil{};
    int deaths{};
    SDK::UCommonActivatableWidget* pooledOpening{};
    bool dismissedOpening{}, openingLeftStack{}, passiveClose{};
    Time closeUntil{}, nextOpeningReview{};
    SDK::APBCharacter* lockedPawn{};
    Time lockedSince{};
    bool closeRetried{}, retryInterrupted{};
};
inline OpenSelection selection;

// 仅记录游戏原生第二项 Cancel；指针只作身份比对，永远不通过保存的地址访问控件。
struct CancelledLeave {
    SDK::APBPlayerController* owner{};
    SDK::APBPlayerState* state{};
    SDK::UCommonActivatableWidget* widget{};
    int deaths{};
    bool leftStack{};
    Time dismissed{};
    SDK::APBCharacter* witnessedPawn{};
    SDK::FName witnessedRole{};
    Time checkUntil{}, missingSince{};
    bool afterCompletedRole{};
};
inline CancelledLeave cancelled;

// 关闭选角页并不等于新角色已能接收输入。只跟踪这一次部署及其本机所有者。
struct InputReturn {
    SDK::APBPlayerController* owner{};
    SDK::APBPlayerState* state{};
    SDK::APBCharacter* pawn{};
    Time expires{}, stable{}, returnedLockSince{};
    bool applied{}, retried{}, restartSent{}, modalBeforeBirth{};
    SDK::FName restartRole{}, expectedRole{}, confirmedRole{};
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
        inputReturn.restartSent = true;
        pc->ServerRestartPlayer();
    }
}

inline void OpenRoles(SDK::APBPlayerController* pc, SDK::APBPlayerState* state)
{
    const int deaths=state->GetDeaths();
    auto* priorLeave=(cancelled.owner==pc && cancelled.state==state && cancelled.leftStack &&
        static_cast<long long>(deaths)-cancelled.deaths==1 && cancelled.widget==gesture.closing) ? cancelled.widget : nullptr;
    cancelled = {};
    selection = {pc, state, state->SelectedCharacterID};
    selection.deaths = deaths;
    selection.pooledOpening = priorLeave;
    selection.escapePrompt = static_cast<SDK::UCommonActivatableWidget*>(const_cast<void*>(gesture.closing));
    selection.promptGrace = Clock::now() + std::chrono::milliseconds(900);
    pc->OpenInGameSelectRoleMenu();
}

inline SDK::APBPlayerController* ObserveNativeSelection(SDK::UObject* sender);

inline void Confirmed(SDK::UObject* sender, SDK::FName role)
{
    if (!(role == SDK::FName{}) && ObserveNativeSelection(sender))
    {
        if (selection.confirmed && !(selection.requestedRole == role)) {
            // 这是另一次实际确认。原干员的持续锁和关页请求不转交给新干员。
            selection.lockedPawn = nullptr;
            selection.closing = selection.passiveClose = selection.closeRetried = selection.retryInterrupted = false;
        }
        if (!selection.confirmed || !(selection.requestedRole == role))
            selection.confirmUntil = Clock::now() + std::chrono::seconds(20);
        selection.requestedRole = role;
        selection.confirmed = true;
    }
}

inline Prompt Classify(SDK::UCommonActivatableWidget*, SDK::APBPlayerController*);
inline void ReadAction(SDK::APBPlayerController*, Frame&);

inline bool OtherMenuLayer(SDK::APBPlayerController* pc, SDK::UCommonActivatableWidget* menu, SDK::UCommonActivatableWidget* inGame)
{
    return (menu && menu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGame && (inGame->GetOwningPlayer() != pc || inGame->Class->GetName() != "InGameOption_V2_C" ||
                    inGame->IsActivated() || inGame->IsVisible()));
}

inline bool ReturnedCancel(SDK::APBPlayerController* pc, SDK::APBPlayerState* state, SDK::UCommonActivatableWidget* top)
{
    return top && cancelled.owner == pc && cancelled.state == state && cancelled.leftStack && top == cancelled.widget &&
        cancelled.deaths == state->GetDeaths() && !pc->IsInGameMenuOpened() && !pc->bShowMouseCursor &&
        !pc->bShowSelectRole && top->GetOwningPlayer() == pc && top->IsVisible() && top->IsActivated() &&
        Classify(top, pc) == Prompt::Leave;
}

inline SDK::APBCharacter* RolePawn(SDK::APBPlayerController* pc, SDK::APBPlayerState* state, const SDK::FName& role)
{
    using namespace SDK;
    if (role==FName{} || !(role==state->SelectedCharacterID) || !(role==state->PossessedCharacterId) ||
        !state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive) || state->bOnlySpectator || state->bIsSpectator || state->bIsABot) return nullptr;
    static const auto playing=UKismetStringLibrary::Conv_StringToName(L"Playing");
    auto* pawn=pc->GetPBCharacter();
    return pc->StateName==playing && pawn && pawn==pc->K2_GetPawn() && pawn==state->PawnPrivate &&
        UKismetSystemLibrary::IsValid(pawn) && pawn->PlayerState==state && pawn->GetController()==pc &&
        pawn->IsAlive() && !pawn->IsInKilledStatus() && pawn->IsFirstPerson() && pc->GetViewTarget()==pawn ? pawn : nullptr;
}

inline SDK::APBCharacter* ConfirmedPawn(SDK::APBPlayerController* pc, SDK::APBPlayerState* state)
{
    return selection.confirmed ? RolePawn(pc,state,selection.requestedRole) : nullptr;
}

inline bool InputLocked(SDK::APBPlayerController* pc)
{
    return pc->IsGameInputIgnored() || pc->IsMoveInputIgnored() || pc->IsLookInputIgnored();
}

inline bool MenuKeyReleased(SDK::APBPlayerController* pc)
{
    Frame key{};
    ReadAction(pc,key);
    return key.keySampled && !key.pressed && !key.down;
}

inline void ReturnGameFocus(SDK::APBPlayerController* pc)
{
    if (pc->IsGameInputIgnored()) pc->ResetIgnoreGameInput();
    if (pc->IsMoveInputIgnored()) pc->ResetIgnoreMoveInput();
    if (pc->IsLookInputIgnored()) pc->ResetIgnoreLookInput();
    pc->bShowMouseCursor = false;
    SDK::UWidgetBlueprintLibrary::SetInputMode_GameOnly(pc);
    SDK::UWidgetBlueprintLibrary::SetFocusToGameViewport();
}

inline void FinishInputReturn(SDK::APBPlayerController* pc, SDK::APBPlayerState* state,
    SDK::UCommonActivatableWidget* prompt, SDK::UCommonActivatableWidget* menu,
    SDK::UCommonActivatableWidget* inGame, Time now)
{
    using namespace SDK;
    auto& r = inputReturn;
    if (r.owner != pc || r.state != state || state->GetDeaths() != r.deaths || now >= r.expires) {
        r = {};
        return;
    }
    if (ReturnedCancel(pc,state,prompt) && !OtherMenuLayer(pc,menu,inGame)) {
        if (auto* current = RolePawn(pc,state,r.confirmedRole)) {
            // 真 Cancel 已经退栈，复用回来的同一个 Leave 不应把这一命的键鼠永久扣住。
            if (!InputLocked(pc)) { r={}; return; }
            if (!r.applied && current==cancelled.witnessedPawn && MenuKeyReleased(pc)) {
                if (r.returnedLockSince==Time{}) r.returnedLockSince=now;
                if (now-r.returnedLockSince < std::chrono::milliseconds(450)) return;
                ReturnGameFocus(pc);
                r={};
                return;
            }
        }
    }
    r.returnedLockSince={};
    if (prompt || pc->IsInGameMenuOpened() || pc->bShowSelectRole ||
        (menu && menu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGame && (inGame->GetOwningPlayer() != pc || inGame->Class->GetName() != "InGameOption_V2_C"))) {
        // 等待出生时短暂出现的菜单只暂停交还。接管角色后玩家新开的菜单仍保留自己的输入。
        if (!r.pawn && state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death)) r.modalBeforeBirth = true;
        else if (!r.modalBeforeBirth) r = {};
        return;
    }
    RequestPendingRestart(pc, state);
    auto* pawn = pc->GetPBCharacter();
    if (!state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive) || !(state->PossessedCharacterId == r.expectedRole) ||
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
    if (!r.applied || (!r.retried && (InputLocked(pc) || pc->bShowMouseCursor))) {
        ReturnGameFocus(pc);
        r.retried = r.applied;
        r.applied = true;
        // 弹窗会短暂离开栈再返回；直到正确 Pawn 真能接收输入，才结束这段跨出生等待。
        r.modalBeforeBirth = false;
    }
    if (r.retried || age >= std::chrono::milliseconds(1200)) r = {};
}

inline void ReturnFromSelection(SDK::APBPlayerController* pc, SDK::APBPlayerState* state,
    SDK::UCommonActivatableWidget* prompt, SDK::UCommonActivatableWidget* menu,
    SDK::UCommonActivatableWidget* inGame)
{
    using namespace SDK;
    if (selection.owner != pc || selection.state != state || selection.deaths != state->GetDeaths())
    {
        selection = {};
        return;
    }
    auto& s=selection;
    const auto now=Clock::now();
    if (s.pooledOpening && !s.passiveClose &&
        (s.confirmed ? now>=s.confirmUntil : now>s.promptGrace+std::chrono::seconds(20))) s.pooledOpening=nullptr;
    if (s.dismissedOpening && s.pooledOpening) {
        // 选择页打开后，原生处理同一个旧 Leave。必须随后亲眼见到提示退栈，而非凭地址猜测它是背景。
        if (!s.openingLeftStack) {
            if (!prompt && !pc->bShowMouseCursor && !pc->IsInGameMenuOpened()) s.openingLeftStack=true;
            else if (prompt && prompt!=s.pooledOpening) s.pooledOpening=nullptr;
        } else {
            Frame key{};ReadAction(pc,key);
            if (key.pressed || key.down || pc->IsInGameMenuOpened() || pc->bShowMouseCursor ||
                (prompt && (prompt!=s.pooledOpening || prompt->GetOwningPlayer()!=pc)) ||
                (menu && menu->Class->GetName()!="UMG_MainMenuGuardian_C") ||
                (inGame && (inGame->GetOwningPlayer()!=pc || inGame->Class->GetName()!="InGameOption_V2_C"))) s.pooledOpening=nullptr;
            else if (prompt && now>=s.nextOpeningReview) {
                s.nextOpeningReview=now+std::chrono::milliseconds(120);
                if (prompt->IsActivated() && Classify(prompt,pc)==Prompt::Other) s.pooledOpening=nullptr;
            }
        }
    }
    if (s.passiveClose) {
        // 跨帧后重新核对当前角色、按键、栈与弹窗内容；仅凭上帧 Close 不能接管新菜单。
        auto* pawn = ConfirmedPawn(pc,state);
        const bool same = pawn && s.pooledOpening && s.openingLeftStack && now < s.confirmUntil &&
            !pc->bShowMouseCursor && !pc->IsInGameMenuOpened() && !OtherMenuLayer(pc,menu,inGame) &&
            (!prompt || (prompt==s.pooledOpening && prompt->IsVisible() && Classify(prompt,pc)==Prompt::Leave)) &&
            MenuKeyReleased(pc);
        if (same && pc->bShowSelectRole) {
            if (now < s.closeUntil) return;
            if (!s.closeRetried && pc->CanSelectRole()) {
                s.closeRetried=true;s.closeUntil=now+std::chrono::milliseconds(450);
                pc->CloseInGameSelectRoleMenu();
                return;
            }
        } else if (same) {
            // 只有同一干员的持续锁允许这一回交还；自然解锁的路径不碰焦点或旧弹窗。
            if (s.lockedPawn == pawn && InputLocked(pc)) ReturnGameFocus(pc);
            // 只把已证实的旧提示身份交给紧接的死亡按键；此记录绝不能授权占有核对。
            cancelled={pc,state,s.pooledOpening,s.deaths,true,now};cancelled.afterCompletedRole=true;
        }
        s={};
        return;
    }
    if (prompt && prompt == selection.escapePrompt && Clock::now() <= selection.promptGrace &&
        Classify(prompt, pc) == Prompt::Leave)
    {
        if (prompt==s.pooledOpening && prompt->IsVisible()) s.dismissedOpening=true;
        selection.escapePrompt = nullptr;
        static_cast<UUMG_Prompt_C*>(prompt)->OnOptionButtonClick(1);
        return;
    }
    if (s.pooledOpening && s.openingLeftStack && prompt==s.pooledOpening && s.confirmed && !s.closing &&
        pc->bShowSelectRole && now<s.confirmUntil && !pc->IsInGameMenuOpened() && !pc->bShowMouseCursor &&
        !OtherMenuLayer(pc,menu,inGame) && pc->CanSelectRole() && MenuKeyReleased(pc) &&
        prompt->GetOwningPlayer()==pc && prompt->IsVisible() && prompt->IsActivated() && Classify(prompt,pc)==Prompt::Leave) {
        if (auto* pawn = ConfirmedPawn(pc,state)) {
            if (InputLocked(pc)) {
                if (s.lockedPawn != pawn) { s.lockedPawn=pawn; s.lockedSince=now; }
                if (now-s.lockedSince < std::chrono::milliseconds(450)) return;
            } else s.lockedPawn=nullptr;
            s.closing=s.passiveClose=true;s.closeUntil=now+std::chrono::milliseconds(450);
            pc->CloseInGameSelectRoleMenu();
            return;
        }
    }
    s.lockedPawn=nullptr;
    if (s.closing && !s.closeRetried && !s.retryInterrupted && pc->bShowSelectRole &&
        (prompt || pc->IsInGameMenuOpened() || OtherMenuLayer(pc,menu,inGame) || !MenuKeyReleased(pc)))
        s.retryInterrupted=true;
    if (prompt || pc->IsInGameMenuOpened() ||
        (menu && menu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGame && (inGame->GetOwningPlayer() != pc || inGame->Class->GetName() != "InGameOption_V2_C")))
        return;
    const bool accepted = !(state->SelectedCharacterID == FName{}) &&
        (selection.confirmed ? state->SelectedCharacterID == selection.requestedRole :
                               !(state->SelectedCharacterID == selection.initialRole));
    if (pc->bShowSelectRole)
    {
        if (!accepted) return;
        if (s.closing) {
            // 第一次原生 Close 可能在重生前被丢弃。只在本人已占有确认的角色后重试一次。
            if (s.closeRetried || s.retryInterrupted || now < s.closeUntil || now >= s.confirmUntil ||
                !ConfirmedPawn(pc,state)) return;
            s.closeRetried=true;
        }
        // 等原生角色选择得到确认后才走正常的关闭流程，下一次采样再读菜单栈。
        selection.closing = true;
        s.closeUntil = now + std::chrono::milliseconds(450);
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
        const int deaths = state->GetDeaths();
        if (inputReturn.owner != pc || inputReturn.state != state || inputReturn.deaths != deaths) {
            inputReturn = {pc, state, nullptr, Clock::now() + std::chrono::seconds(35)};
            inputReturn.deaths = deaths;
        }
        inputReturn.expectedRole = state->SelectedCharacterID;
        inputReturn.confirmedRole = selection.confirmed ? selection.requestedRole : FName{};
        if (selection.confirmed && !inputReturn.restartSent && state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death)) {
            inputReturn.restartRole = selection.requestedRole;
        }
    }
    selection = {};
    pc->bShowMouseCursor = false;
    UWidgetBlueprintLibrary::SetInputMode_GameOnly(pc);
    if (accepted) RequestPendingRestart(pc, state);
}

inline void Travel(SDK::UWorld* world, SDK::UPBLocalPlayer* local, std::string_view target)
{
    ReadingScope scope;
    const bool selected = SDK::UKismetSystemLibrary::ParseParam(
        SDK::UKismetSystemLibrary::GetCommandLine(), L"BrtLocalPveUi");
    room.Travel(selected, target, world, world->OwningGameInstance, local, Clock::now());
    gesture = {};
    selection = {};
    inputReturn = {};
    cancelled = {};
    nextSample = {};
    nextAliveUi = {};
    respawnKeyHeld = false;
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

inline void OptionClicked(SDK::UObject* sender, int option)
{
    using namespace SDK;
    auto& r = inputReturn;
    if (reading || option != 1 || !r.owner || !r.state || !r.modalBeforeBirth || r.applied ||
        !sender || !sender->IsA(UUMG_Prompt_C::StaticClass())) return;
    ReadingScope scope;
    if (!r.owner->IsInGameMenuOpened() || r.deaths != r.state->GetDeaths() ||
        r.owner->PlayerState != r.state || UWorld::GetWorld() != room.playWorld) return;
    auto* top = static_cast<UUMG_Prompt_C*>(sender);
    if (top->GetOwningPlayer() != r.owner || !top->IsActivated() || !top->IsVisible() ||
        Classify(top,r.owner) != Prompt::Leave) return;
    auto* manager = static_cast<UPBMainMenuManager_BP_C*>(USubsystemBlueprintLibrary::GetLocalPlayerSubSystemFromPlayerController(
        r.owner, UPBMainMenuManager_BP_C::StaticClass()));
    if (!manager || !manager->MainMenuLayout || !manager->MainMenuLayout->PromptStack) return;
    UCommonActivatableWidget* active{};
    manager->GetTopPromptWidget(&active);
    if (active != top) return;
    // 蓝图的按钮委托与原生事件有时都能到达；同一次取消只能记一次，不能刷新见证或计时。
    if (!cancelled.afterCompletedRole && cancelled.owner == r.owner && cancelled.state == r.state && cancelled.widget == top && cancelled.deaths == r.deaths) return;
    cancelled = {r.owner, r.state, top, r.deaths};
    if (r.confirmedRole == FName{} || !(r.confirmedRole == r.expectedRole) ||
        !(r.state->SelectedCharacterID == r.expectedRole) || !(r.state->PossessedCharacterId == r.expectedRole) ||
        r.state->bOnlySpectator || r.state->bIsSpectator || r.state->bIsABot ||
        !r.state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive)) return;
    // 真 Cancel 的原生回调前，记下本人此刻已经实际占有的第一人称角色，只用于以后比对。
    auto* pawn = r.owner->GetPBCharacter();
    if (!pawn || pawn != r.owner->K2_GetPawn() || pawn != r.state->PawnPrivate || !UKismetSystemLibrary::IsValid(pawn) ||
        pawn->PlayerState != r.state || pawn->GetController() != r.owner ||
        !pawn->IsAlive() || pawn->IsInKilledStatus() || !pawn->IsFirstPerson()) return;
    cancelled.witnessedPawn = pawn;
    cancelled.witnessedRole = r.expectedRole;
    cancelled.checkUntil = Clock::now() + std::chrono::seconds(4);
}

inline void CheckLostPossession(SDK::APBPlayerController* pc, SDK::APBPlayerState* state,
    SDK::UCommonActivatableWidget* top, SDK::UCommonActivatableWidget* menu,
    SDK::UCommonActivatableWidget* inGame, Time now)
{
    using namespace SDK;
    auto& c = cancelled;
    if (c.afterCompletedRole || !c.witnessedPawn) return;
    // 每次先从当前 PlayerState 取引用；不通过跨帧保存的地址读角色。
    auto* current = state->PawnPrivate;
    if (now >= c.checkUntil || pc != c.owner || state != c.state || state->GetDeaths() != c.deaths ||
        !state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive) || state->bOnlySpectator || state->bIsSpectator || state->bIsABot ||
        !(state->SelectedCharacterID == c.witnessedRole) || !(state->PossessedCharacterId == c.witnessedRole) ||
        current != c.witnessedPawn || !UKismetSystemLibrary::IsValid(current) || current->PlayerState != state) {
        c.witnessedPawn = nullptr;
        return;
    }
    if (!c.leftStack) return;
    Frame action{};
    ReadAction(pc,action);
    if (action.pressed || action.down || pc->IsInGameMenuOpened() || pc->bShowMouseCursor || pc->bShowSelectRole ||
        OtherMenuLayer(pc,menu,inGame) || (top && top != c.widget)) { c.witnessedPawn = nullptr; return; }
    static const auto inactive = UKismetStringLibrary::Conv_StringToName(L"Inactive");
    auto* pawn = static_cast<APBCharacter*>(current);
    if (!(pc->StateName == inactive) || pc->K2_GetPawn() || pc->GetPBCharacter() || pc->AcknowledgedPawn ||
        pc->GetViewTarget() != pc || pawn->GetController() || !pawn->IsAlive() || pawn->IsInKilledStatus() || pawn->IsFirstPerson()) {
        c.missingSince = {};
        return;
    }
    // 同一个控件也可能复用为 Quit 或其他选项。只接受 真 Cancel → 空栈 → 同一原版 Leave。
    if (top && !ReturnedCancel(pc,state,top)) { c.witnessedPawn = nullptr; return; }
    if (c.missingSince == Time{}) { c.missingSince = now; return; }
    if (now - c.missingSince < std::chrono::milliseconds(450)) return;
    c.witnessedPawn = nullptr;
    // 原生无参数 RPC 由本人连接的专服核对当前角色。客户端不指定 Pawn、不写 ACK、不重生、不处理焦点。
    pc->ServerCheckClientPossessionReliable();
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
        if (key.KeyName == SDK::FName{}) return;
        f.keySampled = true;
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

struct RoomPlayer
{
    SDK::APBPlayerController* pc{};
    SDK::APBPlayerState* state{};
    SDK::APBGameState* game{};
};

inline RoomPlayer ResolveRoomPlayer(SDK::UWorld* world, SDK::UPBLocalPlayer* local, Time now)
{
    using namespace SDK;
    if (!world || !local) return {};
    auto* server = world->NetDriver ? world->NetDriver->ServerConnection : nullptr;
    if (!room.Check(world, world->OwningGameInstance, local, server, now))
        return {};
    auto* controller = local->PlayerController;
    if (!controller || !controller->IsA(APBPlayerController::StaticClass()) ||
        controller->Player != local || !controller->IsLocalPlayerController() ||
        (server->OwningActor != controller && server->PlayerController != controller))
        return {};
    auto* pc = static_cast<APBPlayerController*>(controller);
    auto* base = world->GameState;
    if (!base || !base->IsA(APBGameState::StaticClass()) || !base->GameModeClass.Get())
        return {};
    auto* gs = static_cast<APBGameState*>(base);
    if (!room.playWorld)
    {
        if (!IsPveClass(UKismetSystemLibrary::GetPathName(gs->GameModeClass.Get()).ToString()))
        {
            room = {};
            return {};
        }
        room.Bind(world, server, gs->GameModeClass.Get());
    }
    if (room.modeClass != gs->GameModeClass.Get())
    {
        room = {};
        return {};
    }
    if (!pc->PlayerState || !pc->PlayerState->IsA(APBPlayerState::StaticClass()))
        return {};
    auto* state = static_cast<APBPlayerState*>(pc->PlayerState);
    if (state->bOnlySpectator) return {};
    return {pc, state, gs};
}

// 按钮和确认可以在同一帧内开关原生选角。只在本人已经打开的死亡选角页上补记开始。
inline SDK::APBPlayerController* ObserveNativeSelection(SDK::UObject* sender)
{
    using namespace SDK;
    if (!sender || !room.origin) return nullptr;
    if (selection.owner) return selection.owner == sender ? selection.owner : nullptr;
    if (reading) return nullptr;
    ReadingScope scope;
    auto* world = UWorld::GetWorld();
    auto* gi = world ? world->OwningGameInstance : nullptr;
    if (!gi || gi->LocalPlayers.Num() != 1) return nullptr;
    auto who = ResolveRoomPlayer(world, static_cast<UPBLocalPlayer*>(gi->LocalPlayers[0]), Clock::now());
    if (!who.pc || who.pc != sender || !who.pc->bShowSelectRole || !who.game->HasMatchStarted() ||
        who.game->HasMatchEnded() || !who.game->IsRoundInProgress() ||
        !who.state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death) || who.state->GetDeaths() <= 0)
        return nullptr;
    selection = {who.pc, who.state, who.state->SelectedCharacterID};
    selection.deaths = who.state->GetDeaths();
    return who.pc;
}

inline bool Sample(SDK::UWorld* world, SDK::UPBLocalPlayer* local, Time now)
{
    using namespace SDK;
    const auto [pc, state, gs] = ResolveRoomPlayer(world, local, now);
    if (!pc) return false;
    if (inputReturn.owner && (inputReturn.owner != pc || inputReturn.state != state || now >= inputReturn.expires))
        inputReturn = {};
    const bool dead = state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Death);
    // 这里的年龄只决定本机菜单入口；专服按自己的角色出生时间判定装备提交。
    auto* current = !dead && state->GetDeaths() > 0 ? pc->GetPBCharacter() : nullptr;
    const float pawnAge = current && current == pc->K2_GetPawn() && current == state->PawnPrivate
        ? current->GetGameTimeSinceCreation() : -1.0f;
    const bool recentRespawn = pawnAge >= 0.0f && pawnAge < 5.0f;
    static const auto equipmentKey = NamedKey(L"F9");
    bool requestEquipment = false;
    if (recentRespawn && pc->PlayerInput) {
        const bool down = pc->IsInputKeyDown(equipmentKey);
        requestEquipment = !respawnKeyHeld && pc->WasInputKeyJustPressed(equipmentKey);
        respawnKeyHeld = down || requestEquipment;
    } else respawnKeyHeld = false;
    bool observeAlive = !selection.owner && !inputReturn.owner && !dead;
    if (observeAlive)
    {
        nextSample = now + std::chrono::milliseconds(recentRespawn ? 6 : 30);
        if (!state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive)) return false;
        // 存活时仅低频记录本机菜单和按键的干净状态；不打开控件、不改焦点。
        if (!requestEquipment && now < nextAliveUi) return true;
        nextAliveUi = now + std::chrono::milliseconds(120);
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
    if (requestEquipment && !top && !pc->bShowSelectRole && !pc->IsInGameMenuOpened() &&
        !pc->bShowMouseCursor && !OtherMenuLayer(pc,mainMenu,inGameMenu) &&
        RolePawn(pc,state,state->PossessedCharacterId) == current) {
        // 只响应主动按键。菜单继续走原生的单槽预订，不发部署/重生请求。
        selection = {};
        inputReturn = {};
        gesture = {};
        pc->OpenInGameSelectRoleMenu();
        return true;
    }
    if (cancelled.owner) {
        const auto delta = static_cast<long long>(state->GetDeaths()) - cancelled.deaths;
        if (pc != cancelled.owner || state != cancelled.state || delta < 0 || delta > 1) cancelled = {};
        else if (!cancelled.leftStack && !top && !pc->bShowMouseCursor && !pc->IsInGameMenuOpened()) {
            // 原生 Cancel 后要先真正退出过提示栈，不能只凭同一 UObject 推断已关窗。
            cancelled.leftStack = true;
            cancelled.dismissed = now;
        } else if (cancelled.leftStack && ((top && (top != cancelled.widget || top->GetOwningPlayer() != pc)) ||
                   (!dead && now - cancelled.dismissed > std::chrono::milliseconds(300) &&
                    (pc->bShowMouseCursor || pc->IsInGameMenuOpened())))) cancelled = {};
    }
    CheckLostPossession(pc, state, top, mainMenu, inGameMenu, now);
    // 原生流程也能先于 Esc 观察器打开选角。首命不接管，真正死亡后的本机选角才跟踪。
    if (!selection.owner && pc->bShowSelectRole && dead && state->GetDeaths() > 0) {
        selection = {pc, state, state->SelectedCharacterID};
        selection.deaths = state->GetDeaths();
    }
    if (selection.owner)
    {
        const bool completingPassive=selection.passiveClose && !pc->bShowSelectRole;
        ReturnFromSelection(pc, state, top, mainMenu, inGameMenu);
        if (!completingPassive || selection.owner || dead || !state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive)) return true;
        observeAlive=true;nextSample=now+std::chrono::milliseconds(30);nextAliveUi=now+std::chrono::milliseconds(120);
    }
    if (inputReturn.owner) {
        FinishInputReturn(pc, state, top, mainMenu, inGameMenu, now);
        gesture = {};
        if (inputReturn.owner) return true;
        if (!dead) {
            if (!state->IsInPlayerLifeStatus(EPBPlayerLifeStatus::Alive)) return false;
            observeAlive = true;
            nextSample = now + std::chrono::milliseconds(30);
            nextAliveUi = now + std::chrono::milliseconds(120);
        }
    }
    if (!observeAlive && pc->bShowSelectRole) return false;
    Frame frame{};
    frame.controller = pc;
    frame.state = pc->PlayerState;
    frame.layout = layout;
    frame.prompt = top;
    // Guardian 是切图后仍在栈底的大厅占位控件。
    const bool hiddenOldOptions = inGameMenu && inGameMenu->GetOwningPlayer() == pc &&
        inGameMenu->Class->GetName() == "InGameOption_V2_C" && !inGameMenu->IsActivated() && !inGameMenu->IsVisible();
    frame.menuInFront = pc->bShowSelectRole || (mainMenu && mainMenu->Class->GetName() != "UMG_MainMenuGuardian_C") ||
        (inGameMenu && !hiddenOldOptions);
    frame.regularMenuOpen = pc->IsInGameMenuOpened();
    frame.deaths = state->GetDeaths();
    const bool eligibleResidue = cancelled.owner == pc && cancelled.leftStack && top && top == cancelled.widget &&
        top->GetOwningPlayer() == pc && !frame.menuInFront &&
        (observeAlive ? frame.deaths == cancelled.deaths : static_cast<long long>(frame.deaths)-cancelled.deaths == 1);
    if (observeAlive) {
        frame.aliveSnapshot = true;
        frame.cancelledResidue = eligibleResidue && !frame.regularMenuOpen && !pc->bShowMouseCursor;
        if ((!frame.prompt || frame.cancelledResidue) && !frame.menuInFront && !frame.regularMenuOpen) ReadAction(pc, frame);
        gesture.RememberAlive(frame, now);
        return true;
    }
    frame.canSelect = pc->CanSelectRole();
    ReadAction(pc, frame);
    frame.cancelledResidue = eligibleResidue && !frame.regularMenuOpen && !pc->bShowMouseCursor &&
        !frame.pressed && !frame.down;
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
    if (!room.origin || reading || NativeRoleAction::depth)
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
        cancelled = {};
    }
}
}

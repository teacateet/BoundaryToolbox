#include "BrtRoundOwnerComponent.h"
#include "PBPlayerController.h"
#include "PBGameMode.h"
#include "PBGameState.h"
#include "PBPlayerState.h"
#include "PBCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "../BoundaryRound.hpp"

using namespace brt::round;

struct FBrtRoundOwnerState {
    APBPlayerController& PC;
    RoundServer<APBPlayerController, TWeakObjectPtr<APawn>> Server;
    DeathMenu<APBPlayerController, FName> Client;
    explicit FBrtRoundOwnerState(APBPlayerController& InPC) : PC(InPC), Server(InPC), Client(InPC) {}
};

UBrtRoundOwnerComponent::UBrtRoundOwnerComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UBrtRoundOwnerComponent::BeginPlay() {
    Super::BeginPlay();
    if (APBPlayerController* PC = Cast<APBPlayerController>(GetOwner())) {
        State = MakeShared<FBrtRoundOwnerState>(*PC);
        // 处理初始复制早于组件 BeginPlay 的客户端。
        OnRep_RoleWindow();
        if (GetOwner()->HasAuthority()) {
            PC->GetOnNewPawnNotifier().AddUObject(this, &UBrtRoundOwnerComponent::OnPossessedPawn);
            BindGameplayEvents();
        }
    }
}

void UBrtRoundOwnerComponent::EndPlay(const EEndPlayReason::Type Why) {
    if (GetWorld()) {
        GetWorld()->GetTimerManager().ClearTimer(PendingBind);
        GetWorld()->GetTimerManager().ClearTimer(PendingRules);
        GetWorld()->GetTimerManager().ClearTimer(PendingRoleInput);
    }
    if (State.IsValid() && GetOwner()->HasAuthority()) State->PC.GetOnNewPawnNotifier().RemoveAll(this);
    if (APBGameEventManager* Events = BoundEvents.Get()) {
        Events->ServerKillDetailInfoDelegates.RemoveDynamic(this, &UBrtRoundOwnerComponent::OnServerKill);
        Events->ServerSpawnCharacterDelegate.RemoveDynamic(this, &UBrtRoundOwnerComponent::OnServerSpawn);
        Events->GameRoundEndDelegate.RemoveDynamic(this, &UBrtRoundOwnerComponent::OnServerRoundEnd);
    }
    BoundEvents.Reset();
    State.Reset();
    Super::EndPlay(Why);
}

void UBrtRoundOwnerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const {
    Super::GetLifetimeReplicatedProps(Out);
    DOREPLIFETIME_CONDITION(UBrtRoundOwnerComponent, RoleWindow, COND_OwnerOnly);
}

APBGameMode* UBrtRoundOwnerComponent::Mode() const {
    return GetWorld() ? GetWorld()->GetAuthGameMode<APBGameMode>() : nullptr;
}

void UBrtRoundOwnerComponent::BindGameplayEvents() {
    UWorld* World = GetWorld();
    if (!World || !State.IsValid() || !GetOwner()->HasAuthority()) return;
    APBGameState* Game = World->GetGameState<APBGameState>();
    APBGameEventManager* Events = Game ? Game->GetPBGameEventManager() : nullptr;
    if (!IsValid(Events)) {
        World->GetTimerManager().SetTimer(PendingBind, this, &UBrtRoundOwnerComponent::BindGameplayEvents, 0.25f, false);
        return;
    }
    BoundEvents = Events;
    Events->ServerKillDetailInfoDelegates.AddUniqueDynamic(this, &UBrtRoundOwnerComponent::OnServerKill);
    Events->ServerSpawnCharacterDelegate.AddUniqueDynamic(this, &UBrtRoundOwnerComponent::OnServerSpawn);
    Events->GameRoundEndDelegate.AddUniqueDynamic(this, &UBrtRoundOwnerComponent::OnServerRoundEnd);
    OnServerSpawn(Cast<APBPlayerState>(State->PC.PlayerState));
}

void UBrtRoundOwnerComponent::OnServerKill(float, EPBHitZone, TSubclassOf<UDamageType>,
    AController*, AController* Victim, APawn*, APawn* VictimPawn, AActor*) {
    GameplayFinalDeath(Victim, VictimPawn);
}

void UBrtRoundOwnerComponent::OnServerSpawn(APBPlayerState* Player) {
    if (!State.IsValid() || !IsValid(Player) || Player != State->PC.PlayerState) return;
    OnPossessedPawn(State->PC.GetPawn());
}

void UBrtRoundOwnerComponent::OnPossessedPawn(APawn* EnteredPawn) {
    if (!State.IsValid() || !GetOwner()->HasAuthority() || !IsValid(State->PC.PlayerState)) return;
    APBCharacter* NewPawn = Cast<APBCharacter>(EnteredPawn);
    if (!IsValid(NewPawn) || !NewPawn->IsAlive() || NewPawn == ObservedPawn.Get()) return;
    GameplaySpawned(ObservedRound + (ClosedRound ? 1 : 0), ObservedLife + 1, NewPawn);
}

void UBrtRoundOwnerComponent::OnServerRoundEnd(APBPlayerState* Player, EPBGameResult) {
    if (State.IsValid() && IsValid(Player) && Player == State->PC.PlayerState) GameplayRoundEnded(ObservedRound);
}

void UBrtRoundOwnerComponent::Publish() {
    const SelectionState& Window = State->Server.State();
    RoleWindow.Round = Window.life.round;
    RoleWindow.Life = Window.life.number;
    RoleWindow.Revision = Window.revision;
    RoleWindow.Playing = Window.roundPlaying;
    RoleWindow.Dead = Window.finalDeath;
    RoleWindow.Allowed = Window.allowed;
    State->PC.ForceNetUpdate();
    // Listen host 的所属端不会收到自己的属性复制；它在此与远端走相同处理。
    if (State->PC.IsLocalController()) OnRep_RoleWindow();
}

void UBrtRoundOwnerComponent::OnRep_RoleWindow() {
    if (!State.IsValid() || !State->PC.IsLocalController()) return;
    const bool Accepted = State->Client.Receive(State->PC,
        {{RoleWindow.Round, RoleWindow.Life}, RoleWindow.Revision, RoleWindow.Playing,
         RoleWindow.Dead, RoleWindow.Allowed});
    if (Accepted && DisplayLife && (DisplayLife != RoleWindow.Life || DisplayRound != RoleWindow.Round ||
        !RoleWindow.Playing || !RoleWindow.Dead || !RoleWindow.Allowed)) {
        if (FieldOpen) State->PC.CloseInGameSelectRoleMenu();
        FieldModClosedOrFailed();
    }
    if (Accepted) RecheckRoleInput();
}

bool UBrtRoundOwnerComponent::GameplaySpawned(uint64 Round, uint64 Life, APawn* NewPawn) {
    APBGameMode* Rules = Mode();
    if (!State.IsValid() || !Rules || !IsValid(NewPawn) || State->PC.GetPawn() != NewPawn) return false;
    const bool Accepted = State->Server.Spawn(State->PC, {Round, Life}, TWeakObjectPtr<APawn>(NewPawn), *Rules,
        [this](auto&, const auto&) { Publish(); });
    if (Accepted) {
        ObservedPawn = NewPawn;
        ObservedRound = Round;
        ObservedLife = Life;
        ClosedRound = false;
        RecheckRules();
    }
    return Accepted;
}

bool UBrtRoundOwnerComponent::GameplayFinalDeath(AController* Victim, APawn* DeadPawn) {
    APBGameMode* Rules = Mode();
    if (!State.IsValid() || !Rules || Victim != &State->PC || !DeadPawn) return false;
    const bool Changed = State->Server.FinalDeath(&State->PC, State->Server.State().life,
        TWeakObjectPtr<APawn>(DeadPawn), *Rules, [this](auto&, const auto&) { Publish(); });
    if (Changed) RecheckRules();
    return Changed;
}

void UBrtRoundOwnerComponent::GameplayRulesChanged() {
    APBGameMode* Rules = Mode();
    if (State.IsValid() && Rules)
        State->Server.Refresh(State->Server.State().life, *Rules, [this](auto&, const auto&) { Publish(); });
}

void UBrtRoundOwnerComponent::RecheckRules() {
    UWorld* World = GetWorld();
    if (!World || !State.IsValid() || !GetOwner()->HasAuthority()) return;
    World->GetTimerManager().ClearTimer(PendingRules);
    if (ClosedRound) return;
    GameplayRulesChanged();
    const SelectionState& Window = State->Server.State();
    // 规则可能在死亡回调之后开放。出生前等待开局或最终死亡时，才检查权限变化。
    if (Window.life.Valid() && (!Window.roundPlaying || Window.finalDeath))
        World->GetTimerManager().SetTimer(PendingRules, this, &UBrtRoundOwnerComponent::RecheckRules, 0.25f, false);
}

void UBrtRoundOwnerComponent::GameplayRoundEnded(uint64 Round) {
    if (State.IsValid() && Mode() && State->Server.End(Round, [this](auto&, const auto&) { Publish(); })) {
        ClosedRound = true;
        GetWorld()->GetTimerManager().ClearTimer(PendingRules);
    }
}

bool UBrtRoundOwnerComponent::ShowDeathRolePrompt(bool SupportedMode) const {
    return State.IsValid() && State->Client.OffersRole(State->PC, SupportedMode);
}

void UBrtRoundOwnerComponent::RememberDisplayedLife() {
    DisplayRound = State->Client.State().life.round;
    DisplayLife = State->Client.State().life.number;
}

bool UBrtRoundOwnerComponent::HandleInGameMenu(bool SupportedMode, bool HigherModal,
                                               bool FieldModOpen, bool IsRepeat) {
    if (!State.IsValid()) return false;
    SupportsRole = SupportedMode;
    const auto result = State->Client.Press(State->PC, SupportedMode, HigherModal, FieldModOpen, IsRepeat,
        [this] { RememberDisplayedLife(); });
    if (UWorld* World = GetWorld()) {
        auto& timers = World->GetTimerManager();
        if (!State.IsValid() || !State->Client.HasPendingPress()) timers.ClearTimer(PendingRoleInput);
        else if (!timers.IsTimerActive(PendingRoleInput))
            timers.SetTimer(PendingRoleInput, this, &UBrtRoundOwnerComponent::RecheckRoleInput, 0.10f, true);
    }
    return result == KeyResult::Consumed;
}

void UBrtRoundOwnerComponent::RecheckRoleInput() {
    if (!State.IsValid() || !GetWorld()) return;
    State->Client.ResumePress(State->PC, SupportsRole, HasModal, FieldOpen, [this] { RememberDisplayedLife(); });
    if (GetWorld() && (!State.IsValid() || !State->Client.HasPendingPress()))
        GetWorld()->GetTimerManager().ClearTimer(PendingRoleInput);
}

FInputActionBinding& UBrtRoundOwnerComponent::BindInGameMenuAction(UInputComponent& Input,
    bool StockOpensExit, bool SupportedMode) {
    StockExit = StockOpensExit;
    SupportsRole = SupportedMode;
    return Input.BindAction(TEXT("InGameMenu"), IE_Pressed, this, &UBrtRoundOwnerComponent::OnInGameMenuPressed);
}

void UBrtRoundOwnerComponent::SetMenuFocus(bool HigherModal, bool FieldModOpen) {
    HasModal = HigherModal;
    if (FieldOpen && !FieldModOpen) FieldModClosedOrFailed();
    FieldOpen = FieldModOpen;
    if (HigherModal || FieldModOpen) {
        if (State.IsValid()) State->Client.CancelPendingPress();
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PendingRoleInput);
    }
}

void UBrtRoundOwnerComponent::OnInGameMenuPressed() {
    APBPlayerController* PC = Cast<APBPlayerController>(GetOwner());
    if (PC) DispatchInGameMenu(*PC, HasModal, FieldOpen && DisplayLife, StockExit,
        [this] { return HandleInGameMenu(SupportsRole, HasModal, FieldOpen, false); },
        [this] { FieldModClosedOrFailed(); });
}

void UBrtRoundOwnerComponent::FieldModClosedOrFailed() {
    if (State.IsValid()) {
        State->Client.OnMenuDismissedOrOpenFailed();
        State->Client.OnSelectionResolved({DisplayRound, DisplayLife});
    }
    DisplayRound = DisplayLife = 0;
    FieldOpen = false;
}

bool UBrtRoundOwnerComponent::ConfirmDisplayedRole(FName Role, bool SupportedMode) {
    return State.IsValid() && !Role.IsNone() &&
        State->Client.Confirm(State->PC, {DisplayRound, DisplayLife}, Role, SupportedMode);
}

bool UBrtRoundOwnerComponent::GetLivingView(uint64& Round, uint64& Life) const {
    if (!State.IsValid() || !State->PC.IsLocalController()) return false;
    const SelectionState& Window = State->Client.State();
    if (!Window.life.Valid() || !Window.roundPlaying || Window.finalDeath) return false;
    Round = Window.life.round;
    Life = Window.life.number;
    return true;
}

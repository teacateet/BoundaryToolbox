#include "BrtHelmetRadarWidget.h"
#include "BrtRoundOwnerComponent.h"
#include "PBCharacter.h"
#include "PBPlayerController.h"
#include "PBPlayerState.h"

void UBrtHelmetRadarWidget::ClearLocalRadar() {
    Radar.Detach();
    View = {};
    ObservingPawn.Reset();
    ObservingTeam = INDEX_NONE;
}

void UBrtHelmetRadarWidget::NativeDestruct() {
    ClearLocalRadar();
    Super::NativeDestruct();
}

void UBrtHelmetRadarWidget::BeginLocalRadarCapture() {
    APBPlayerController* PC = Cast<APBPlayerController>(GetOwningPlayer());
    APBCharacter* Pawn = Cast<APBCharacter>(GetOwningPlayerPawn());
    APBPlayerState* Player = PC ? Cast<APBPlayerState>(PC->PlayerState) : nullptr;
    UBrtRoundOwnerComponent* Owner = PC ? PC->FindComponentByClass<UBrtRoundOwnerComponent>() : nullptr;
    uint64 Round = 0, Life = 0;
    if (!IsValid(PC) || !PC->IsLocalController() || !IsValid(Pawn) || Pawn != OnwerCharacter ||
        !Pawn->IsAlive() || !IsValid(Player) || !Owner || !Owner->GetLivingView(Round, Life)) {
        ClearLocalRadar();
        return;
    }
    const int32 Team = Player->GetTeamId();
    if (Team == INDEX_NONE) {
        ClearLocalRadar();
        return;
    }
    const brt::round::Life Expected{Round, Life};
    if (View.life != Expected || ObservingTeam != Team || ObservingPawn.Get() != Pawn) {
        View = {Expected, ++ViewSerial};
        ObservingPawn = Pawn;
        ObservingTeam = Team;
        Radar.SetView(*this, View);
    }
    Radar.BeginCapture(View);
}

void UBrtHelmetRadarWidget::EligibleInNativeDraw(UPBMarkableComponent* Target) {
    Radar.NoteEligible(Target);
}
void UBrtHelmetRadarWidget::DrewNativeNode(UPBMarkableComponent* Target) {
    Radar.NoteDrawn(Target);
}
void UBrtHelmetRadarWidget::EndLocalRadarCapture() {
    Radar.FinishCapture(View,
        [this]() -> UPBMiniMapNode* {
            if (!UPBMiniMapNodeClass) return nullptr;
            return CreateWidget<UPBMiniMapNode>(GetOwningPlayer(), UPBMiniMapNodeClass);
        },
        [this](UPBMiniMapNode* Node) { K2_AddWidgetToCanvasPanel(Node); });
}

#pragma once
#include "CoreMinimal.h"
#include "ProjectTypes.generated.h"

enum class EPBHandsState {
    EmptyHand, DeployedWeapon, DeployingWeapon, UndeployingLauncherController,
    DeployedLauncherController, DeployingLauncherController, FixingAirTightness
};
class UPBLauncherWidget;
enum class EPBLauncherState {Standby, Deploying, Undeploying, Ready, Reloading, Handup};
class PROJECTBOUNDARY_API APBLauncher : public AActor {
    GENERATED_BODY()
public:
    APawn* Owner = nullptr;
    APawn* GetOwnerPawn() const { return Owner; }
    bool Standby = false;
    bool Undeploy = false;
    EPBLauncherState CurrentState = EPBLauncherState::Standby;
    bool bIsFiring=false, bPendingFiring=false, bInProjectileControlMode=false, bInSpecialMode=false;
    bool Active=false;
    bool IsActivated() const { return Active; }
    bool IsStandby() { return Standby; }
    bool IsUndeploying() { return Undeploy; }
    USkeletalMeshComponent* ArmMesh1P = nullptr;
    USkeletalMeshComponent* ArmMesh3P = nullptr;
    UStaticMeshComponent* Mesh1P = nullptr;
    UStaticMeshComponent* Mesh3P = nullptr;
    UPBLauncherWidget* CrossHairWidget = nullptr;
};
class PROJECTBOUNDARY_API APBLauncher_Deploy : public APBLauncher {
    GENERATED_BODY()
};
class PROJECTBOUNDARY_API APBCharacter : public APawn {
    GENERATED_BODY()
public:
    bool Living = true;
    bool IsAlive() const { return Living; }
    APBLauncher* CurrentLeftLauncher = nullptr;
    APBLauncher* CurrentRightLauncher = nullptr;
    APBLauncher* CurrentLauncher = nullptr;
    EPBHandsState CurrentHandsState = EPBHandsState::EmptyHand;
};
class PROJECTBOUNDARY_API UPBLauncherWidget : public UUserWidget {
    GENERATED_BODY()
public:
    using Super = UUserWidget;
    APBLauncher* OwningLauncher = nullptr;
    int OriginalRebuilds = 0;
    TSharedRef<SWidget> RebuildWidget() override;
};
class PROJECTBOUNDARY_API APBPlayerController : public APlayerController {
    GENERATED_BODY()
public:
    using Super = APlayerController;
};

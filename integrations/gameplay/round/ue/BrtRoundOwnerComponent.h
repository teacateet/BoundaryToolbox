#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/InputComponent.h"
#include "Serialization/Archive.h"
#include "PBGameEventManager.h"
#include "BrtRoundOwnerComponent.generated.h"

class APBGameMode;
class APBPlayerController;
class AController;
class APawn;
class UPackageMap;
struct FBrtRoundOwnerState;

// 该结构位于玩家自己的控制器上，仅向所属连接复制。
USTRUCT()
struct FBrtRoundRoleWindow {
    GENERATED_BODY()
    UPROPERTY() uint64 Round = 0;
    UPROPERTY() uint64 Life = 0;
    UPROPERTY() uint64 Revision = 0;
    UPROPERTY() bool Playing = false;
    UPROPERTY() bool Dead = false;
    UPROPERTY() bool Allowed = false;
    bool NetSerialize(FArchive& Ar, UPackageMap*, bool& Succeeded) {
        Ar << Round << Life << Revision;
        uint8 Flags = (Playing ? 1 : 0) | (Dead ? 2 : 0) | (Allowed ? 4 : 0);
        Ar.SerializeBits(&Flags, 3);
        if (Ar.IsLoading()) {
            Playing = (Flags & 1) != 0;
            Dead = (Flags & 2) != 0;
            Allowed = (Flags & 4) != 0;
        }
        Succeeded = !Ar.IsError();
        return true;
    }
};
template<> struct TStructOpsTypeTraits<FBrtRoundRoleWindow> : TStructOpsTypeTraitsBase2<FBrtRoundRoleWindow> {
    enum { WithNetSerializer = true };
};

UCLASS(ClassGroup=(Boundary), meta=(BlueprintSpawnableComponent))
class UBrtRoundOwnerComponent : public UActorComponent {
    GENERATED_BODY()
public:
    UBrtRoundOwnerComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

    // 游戏已有的服务器回调传入该命的受控 Pawn 与出生序号。重复或过期事件返回 false。
    bool GameplaySpawned(uint64 Round, uint64 Life, APawn* NewPawn);
    bool GameplayFinalDeath(AController* KilledControl, APawn* KilledPawn);
    void GameplayRulesChanged();
    void GameplayRoundEnded(uint64 Round);

    // 在现有 InGameMenu 动作中调用一次。true 表示已经处理本次输入。
    UFUNCTION(BlueprintCallable, Category="Boundary|Role")
    bool HandleInGameMenu(bool SupportedMode, bool HigherModal, bool FieldModOpen, bool IsKeyRepeat);
    // 在控制器原有的按下绑定处返回同一类 binding。原有松键、焦点和回合重绑仍由控制器管理。
    FInputActionBinding& BindInGameMenuAction(UInputComponent& Input, bool StockOpensExit,
                                              bool SupportedMode);
    UFUNCTION(BlueprintCallable, Category="Boundary|Role")
    void SetMenuFocus(bool HigherModal, bool FieldModOpen);
    UFUNCTION(BlueprintPure, Category="Boundary|Role")
    bool ShowDeathRolePrompt(bool SupportedMode) const;
    UFUNCTION(BlueprintCallable, Category="Boundary|Role")
    void FieldModClosedOrFailed();
    UFUNCTION(BlueprintCallable, Category="Boundary|Role")
    bool ConfirmDisplayedRole(FName Role, bool SupportedMode);
    bool GetLivingView(uint64& Round, uint64& Life) const;

private:
    UPROPERTY(ReplicatedUsing=OnRep_RoleWindow) FBrtRoundRoleWindow RoleWindow;
    UFUNCTION() void OnRep_RoleWindow();
    void Publish();
    APBGameMode* Mode() const;
    void BindGameplayEvents();
    void OnPossessedPawn(APawn* Pawn);
    UFUNCTION() void OnServerKill(float Damage, EPBHitZone HitZone, TSubclassOf<UDamageType> DamageType,
        AController* KillerControl, AController* KilledControl, APawn* KillerPawn, APawn* KilledPawn, AActor* Cause);
    UFUNCTION() void OnServerSpawn(APBPlayerState* Player);
    UFUNCTION() void OnServerRoundEnd(APBPlayerState* Player, EPBGameResult Result);
    TWeakObjectPtr<APBGameEventManager> BoundEvents;
    TWeakObjectPtr<APawn> ObservedPawn;
    FTimerHandle PendingBind;
    FTimerHandle PendingRules;
    FTimerHandle PendingRoleInput;
    void RecheckRules();
    void RecheckRoleInput();
    void RememberDisplayedLife();
    uint64 ObservedRound = 1;
    uint64 ObservedLife = 0;
    uint64 DisplayRound = 0;
    uint64 DisplayLife = 0;
    bool ClosedRound = false;
    bool StockExit = true;
    bool SupportsRole = false;
    bool HasModal = false;
    bool FieldOpen = false;
    void OnInGameMenuPressed();
    TSharedPtr<FBrtRoundOwnerState> State;
};

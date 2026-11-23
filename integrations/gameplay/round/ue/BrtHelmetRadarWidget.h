#pragma once

#include "CoreMinimal.h"
#include "PBHelmetHUD_MiniMap.h"
#include "BrtMarkedRadar.h"
#include "BrtHelmetRadarWidget.generated.h"

// 被本地角色的常规小地图控件使用。沿用原有 HUD 蓝图的节点类、画布与动画。
UCLASS(Abstract, Blueprintable)
class UBrtHelmetRadarWidget : public UPBHelmetHUD_MiniMap {
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Boundary|Radar") void BeginLocalRadarCapture();
    UFUNCTION(BlueprintCallable, Category="Boundary|Radar") void EligibleInNativeDraw(UPBMarkableComponent* Target);
    UFUNCTION(BlueprintCallable, Category="Boundary|Radar") void DrewNativeNode(UPBMarkableComponent* Target);
    UFUNCTION(BlueprintCallable, Category="Boundary|Radar") void EndLocalRadarCapture();
    UFUNCTION(BlueprintCallable, Category="Boundary|Radar") void ClearLocalRadar();
    virtual void NativeDestruct() override;
private:
    FBrtMarkedRadar Radar;
    brt::round::RadarView View{};
    uint64 ViewSerial = 0;
    int32 ObservingTeam = INDEX_NONE;
    TWeakObjectPtr<APawn> ObservingPawn;
};

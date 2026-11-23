// 声明加入 APBCatchingModule 类体，让 UHT 直接生成正常的拥有者 RPC。
public:
    void BrtRefreshServerLife();
    void BrtEndServerLife();
    void BrtOwnerPress();
    void BrtOwnerRelease();
    void BrtOwnerLaunchTarget(const FPBCaughtData& InData);
    FBrtGrappleUse BrtOwnerRequest() const;
    FBrtGrappleUse BrtNativeLaunchUse() const;
    bool BrtClaimValidatedLaunch(FBrtGrappleUse Key);
    bool BrtNativeLaunchFailed(FBrtGrappleUse Key);
    bool BrtNativeLaunched(FBrtGrappleUse Key);
    bool BrtNativeLocked(FBrtGrappleUse Key);
    bool BrtNativeRecycling(FBrtGrappleUse Key);
    bool BrtNativeReturned(FBrtGrappleUse Key);
    bool BrtAcknowledgeReturned(FBrtGrappleUse Key);
    void BrtObserveNativeLifecycle();
private:
    UFUNCTION(Server, Reliable, WithValidation)
    void BrtServerInputEdge(uint32 Life, uint32 Use, bool bPressed);
    UFUNCTION(Server, Reliable, WithValidation)
    void BrtServerLaunchTagged(uint32 Life, uint32 Use, const FPBCaughtData& InData);
    UFUNCTION(Client, Reliable)
    void BrtClientAvailability(uint32 Life, uint32 Use);
    bool BrtHasLiveAuthority() const;
    bool BrtStartReturnCooldown(FBrtGrappleUse Key, EBrtGrappleReturn Evidence);
    void BrtCooldownTimer(FBrtGrappleUse Key);
    FBrtGrappleAuthority BrtAuthority;
    FBrtGrappleOwnerInput BrtOwnerInput;
    FBrtGrappleUse BrtNativeUse;
    bool bBrtPullDelivered = false;
    bool bBrtReturnDelivered = false;
    bool bBrtCooldownStartDelivered = false;
    TWeakObjectPtr<APBCharacter> BrtBoundOwner;
    FTimerHandle BrtReturnTimer;

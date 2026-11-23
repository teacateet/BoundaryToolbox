// APBCatchingModule 的正常网络拥有者、游戏线程和 FTimerManager 入口。
bool APBCatchingModule::BrtHasLiveAuthority() const
{
    return HasAuthority() && IsValid(OwnerCharacter) && OwnerCharacter == BrtBoundOwner.Get()
        && OwnerCharacter->IsAlive() && OwnerCharacter->GetCurrentMobilityModule() == this;
}

void APBCatchingModule::BrtRefreshServerLife()
{
    if (!HasAuthority()) return;
    if (BrtHasLiveAuthority()) return;
    BrtEndServerLife();
    if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsAlive()
        || OwnerCharacter->GetCurrentMobilityModule() != this) return;
    BrtBoundOwner = OwnerCharacter;
    const FBrtGrappleUse Key = BrtAuthority.BindNewLife();
    if (Key.Valid()) BrtClientAvailability(Key.Life, Key.Use);
}

void APBCatchingModule::BrtEndServerLife()
{
    if (!HasAuthority()) return;
    const FBrtGrappleUse Old = BrtAuthority.Current();
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(BrtReturnTimer);
    BrtAuthority.Unbind();
    BrtNativeUse = {};
    BrtBoundOwner.Reset();
    if (Old.Valid())
    {
        bIsInCoolDown = false;
        BrtClientAvailability(Old.Life, 0);
    }
}

void APBCatchingModule::BrtClientAvailability_Implementation(uint32 Life, uint32 Use)
{
    if (Use == 0)
    {
        if (BrtOwnerInput.Retire(Life)) bIsInCoolDown = false;
    }
    else BrtOwnerInput.ObserveReady({Life, Use});
}

void APBCatchingModule::BrtOwnerPress()
{
    if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsLocallyControlled()
        || !OwnerCharacter->IsAlive() || OwnerCharacter->GetCurrentMobilityModule() != this) return;
    const FBrtGrappleUse Key = BrtOwnerInput.Press();
    if (Key.Valid()) BrtServerInputEdge(Key.Life, Key.Use, true);
}

void APBCatchingModule::BrtOwnerRelease()
{
    if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsLocallyControlled()) return;
    const FBrtGrappleUse Key = BrtOwnerInput.Release();
    if (Key.Valid()) BrtServerInputEdge(Key.Life, Key.Use, false);
}

FBrtGrappleUse APBCatchingModule::BrtOwnerRequest() const
{
    return BrtOwnerInput.Action();
}

void APBCatchingModule::BrtOwnerLaunchTarget(const FPBCaughtData& InData)
{
    if (!IsValid(OwnerCharacter) || !OwnerCharacter->IsLocallyControlled()
        || !OwnerCharacter->IsAlive() || OwnerCharacter->GetCurrentMobilityModule() != this) return;
    const FBrtGrappleUse Key = BrtOwnerRequest();
    if (Key.Valid()) BrtServerLaunchTagged(Key.Life, Key.Use, InData);
}

bool APBCatchingModule::BrtServerLaunchTagged_Validate(uint32 Life, uint32 Use, const FPBCaughtData& InData)
{
    return Life != 0 && Use != 0 && ServerLaunchHook_Validate(InData);
}

void APBCatchingModule::BrtServerLaunchTagged_Implementation(uint32 Life, uint32 Use, const FPBCaughtData& InData)
{
    const FBrtGrappleUse Key{Life, Use};
    if (!BrtClaimValidatedLaunch(Key)) return;
    BrtNativeUse = Key;
    bBrtPullDelivered = false;
    bBrtReturnDelivered = false;
    bBrtCooldownStartDelivered = false;
    // 原服务器发射实现继续执行其目标、障碍和角色条件检查，以及真实游戏的运动与特效。
    ServerLaunchHook_Implementation(InData);
}

FBrtGrappleUse APBCatchingModule::BrtNativeLaunchUse() const
{
    // 只在原始发射开始时读取一次，让该发的运动与回收回调按值保存它。
    return BrtNativeUse;
}

bool APBCatchingModule::BrtServerInputEdge_Validate(uint32 Life, uint32 Use, bool)
{
    // 乱序或失效的可靠消息是正常游戏状态，在实现中丢弃，不能因此断开原玩家。
    return Life != 0 && Use != 0;
}

void APBCatchingModule::BrtServerInputEdge_Implementation(uint32 Life, uint32 Use, bool bPressed)
{
    if (!BrtHasLiveAuthority()) return;
    if (bPressed)
    {
        const FBrtGrappleUse Key{Life,Use};
        if (BrtAuthority.Press(Key) && (!bIsEnabled || bIsInCoolDown || CurrentHookStatus != EPBHookStatus::Ready))
            BrtNativeLaunchFailed(Key);
    }
    else BrtAuthority.Release({Life, Use});
}

bool APBCatchingModule::BrtClaimValidatedLaunch(FBrtGrappleUse Key)
{
    if (!BrtHasLiveAuthority() || !BrtAuthority.AwaitingLaunch(Key)) return false;
    if (!bIsEnabled || bIsInCoolDown || CurrentHookStatus != EPBHookStatus::Ready)
    {
        BrtNativeLaunchFailed(Key);
        return false;
    }
    return BrtAuthority.ClaimValidatedLaunch(Key);
}

bool APBCatchingModule::BrtNativeLaunchFailed(FBrtGrappleUse Key)
{
    if (!BrtHasLiveAuthority() || !BrtAuthority.CancelOrFail(Key)) return false;
    const FBrtGrappleUse Next = BrtAuthority.Current();
    BrtClientAvailability(Next.Life, Next.Use);
    return true;
}

bool APBCatchingModule::BrtNativeLaunched(FBrtGrappleUse Key)
{
    return BrtHasLiveAuthority() && CurrentHookStatus == EPBHookStatus::Launching && BrtAuthority.NativeLaunch(Key);
}

bool APBCatchingModule::BrtNativeLocked(FBrtGrappleUse Key)
{
    if (!BrtHasLiveAuthority() || !(Key == BrtAuthority.Current()) || bBrtPullDelivered
        || CurrentHookStatus != EPBHookStatus::Locked) return false;
    BrtAuthority.NativeLock(Key);
    if (BrtAuthority.State() != EBrtGrapplePhase::Locked) return false;
    bBrtPullDelivered = true;
    return true;
}

bool APBCatchingModule::BrtNativeRecycling(FBrtGrappleUse Key)
{
    if (!BrtHasLiveAuthority() || !(Key == BrtAuthority.Current()) || bBrtReturnDelivered
        || CurrentHookStatus != EPBHookStatus::Recycling) return false;
    BrtAuthority.NativeRecycle(Key);
    if (BrtAuthority.State() != EBrtGrapplePhase::Recycling) return false;
    bBrtReturnDelivered = true;
    return true;
}

bool APBCatchingModule::BrtNativeReturned(FBrtGrappleUse Key)
{
    return BrtStartReturnCooldown(Key, EBrtGrappleReturn::NativeCallback);
}

bool APBCatchingModule::BrtStartReturnCooldown(FBrtGrappleUse Key, EBrtGrappleReturn Evidence)
{
    UWorld* World = GetWorld();
    if (!World || !BrtHasLiveAuthority() || CurrentHookStatus != EPBHookStatus::Ready
        || !BrtAuthority.NativeReturned(Key, World->GetTimeSeconds(), DataConfig.HookCoolDown, Evidence)) return false;
    bIsInCoolDown = true;
    // 新计时器取代原生冷却计时器。在调用返回 true 后执行游戏原有的冷却开始通知。
    FTimerDelegate Expiry = FTimerDelegate::CreateUObject(this, &APBCatchingModule::BrtCooldownTimer, Key);
    World->GetTimerManager().SetTimer(BrtReturnTimer, Expiry, DataConfig.HookCoolDown, false);
    return true;
}

bool APBCatchingModule::BrtAcknowledgeReturned(FBrtGrappleUse Key)
{
    if (!BrtHasLiveAuthority() || !(Key == BrtAuthority.Current()) || bBrtCooldownStartDelivered
        || CurrentHookStatus != EPBHookStatus::Ready) return false;
    if (!BrtNativeReturned(Key) && BrtAuthority.State() != EBrtGrapplePhase::Cooling) return false;
    bBrtCooldownStartDelivered = true;
    return true;
}

void APBCatchingModule::BrtObserveNativeLifecycle()
{
    if (!HasAuthority()) return;
    BrtRefreshServerLife();
    if (!BrtHasLiveAuthority() || !BrtNativeUse.Valid()) return;
    // 在原有 Actor Tick 完成之后读取真实权威状态。已在同帧办理的玩法回调会被状态机去重。
    switch (CurrentHookStatus)
    {
        case EPBHookStatus::Launching:
            BrtNativeLaunched(BrtNativeUse);
            break;
        case EPBHookStatus::Locked:
            BrtAuthority.NativeLock(BrtNativeUse);
            break;
        case EPBHookStatus::Recycling:
            BrtAuthority.NativeRecycle(BrtNativeUse);
            break;
        case EPBHookStatus::Ready:
            BrtStartReturnCooldown(BrtNativeUse, EBrtGrappleReturn::SampledReady);
            break;
        default:
            break;
    }
}

void APBCatchingModule::BrtCooldownTimer(FBrtGrappleUse Key)
{
    UWorld* World = GetWorld();
    if (!World || !BrtHasLiveAuthority() || !(BrtAuthority.Current() == Key)
        || BrtAuthority.State() != EBrtGrapplePhase::Cooling) return;
    const double Now = World->GetTimeSeconds();
    if (!BrtAuthority.TimerExpired(Key, Now))
    {
        const float Remaining = static_cast<float>(BrtAuthority.Deadline() - Now);
        if (Remaining > 0.0f)
        {
            FTimerDelegate Expiry = FTimerDelegate::CreateUObject(this, &APBCatchingModule::BrtCooldownTimer, Key);
            World->GetTimerManager().SetTimer(BrtReturnTimer, Expiry, Remaining, false);
        }
        return;
    }
    bIsInCoolDown = false;
    BrtNativeUse = {};
    ClientEndHookLockCoolDown();
    const FBrtGrappleUse Next = BrtAuthority.Current();
    BrtClientAvailability(Next.Life, Next.Use);
}

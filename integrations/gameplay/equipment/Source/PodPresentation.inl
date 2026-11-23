// 此文件由 ProjectBoundary 游戏模块编译。调用处使用原有 UE 对象与视口。
namespace BrtPodPresentation
{
    static void AddOwnedPrimitives(const USceneComponent* Root, const AActor* Launcher,
        const USceneComponent* ThirdPersonArm, const USceneComponent* ThirdPersonMesh,
        TSet<FPrimitiveComponentId>& Hidden)
    {
        if (!IsValid(Root) || Root == ThirdPersonArm || Root == ThirdPersonMesh || Root->GetOwner() != Launcher) return;
        if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Root))
        {
            if (Primitive->IsRegistered()) Hidden.Add(Primitive->ComponentId);
        }
        for (const USceneComponent* Child : Root->GetAttachChildren())
            AddOwnedPrimitives(Child, Launcher, ThirdPersonArm, ThirdPersonMesh, Hidden);
    }
}

bool APBCharacter::BrtIsPresentingPod(APBLauncher* Pod) const
{
    if (!IsValid(Pod) || !IsAlive() || Pod->GetOwnerPawn() != this
        || (CurrentLeftLauncher != Pod && CurrentRightLauncher != Pod)) return false;
    const bool bSelected = CurrentLauncher == Pod;
    const bool bControllerHands = CurrentHandsState == EPBHandsState::DeployingLauncherController
        || CurrentHandsState == EPBHandsState::DeployedLauncherController;
    if (Cast<APBLauncher_Deploy>(Pod))
        return bSelected && bControllerHands && !Pod->IsStandby() && !Pod->IsUndeploying();
    // 自动、延时和直接发射的挂载并不都抢用持枪手。其活动状态只解除视图过滤，原组件决定是否绘制。
    return Pod->BrtHasAuxiliaryPresentation(bSelected);
}

bool APBLauncher::BrtHasAuxiliaryPresentation(bool bSelected) const
{
    if (CurrentState == EPBLauncherState::Undeploying) return false;
    if (bIsFiring || bPendingFiring || bInProjectileControlMode || bInSpecialMode) return true;
    if (CurrentState == EPBLauncherState::Deploying || CurrentState == EPBLauncherState::Handup) return true;
    return (bSelected || IsActivated())
        && (CurrentState == EPBLauncherState::Ready || CurrentState == EPBLauncherState::Reloading);
}

void APBCharacter::BrtRememberLocalPods(TMap<TWeakObjectPtr<APBLauncher>, TWeakObjectPtr<APBCharacter>>& Seen)
{
    for (APBLauncher* Pod : {CurrentLeftLauncher, CurrentRightLauncher})
    {
        if (IsValid(Pod) && Pod->GetOwnerPawn() == this)
            Seen.FindOrAdd(TWeakObjectPtr<APBLauncher>(Pod)) = this;
    }
}

void APBLauncher::BrtHideFirstPersonPod(TSet<FPrimitiveComponentId>& Hidden) const
{
    // 只在该玩家视图中过滤挂载自身的第一人称模型及挂件。已投出的探针拥有自己的 Actor。
    BrtPodPresentation::AddOwnedPrimitives(ArmMesh1P, this, ArmMesh3P, Mesh3P, Hidden);
    BrtPodPresentation::AddOwnedPrimitives(Mesh1P, this, ArmMesh3P, Mesh3P, Hidden);
}

bool APBLauncher::BrtOwnsReticle(const UPBLauncherWidget* Widget) const
{
    return Widget && CrossHairWidget == Widget;
}

void APBLauncher::BrtRefreshReticleLayout()
{
    UPBLauncherWidget* Prior = BrtPreviousReticle.Get();
    if (Prior && Prior != CrossHairWidget) Prior->BrtRefreshReticleLayout(nullptr);
    if (IsValid(CrossHairWidget)) CrossHairWidget->BrtRefreshReticleLayout(this);
    BrtPreviousReticle = CrossHairWidget;
}

bool UPBLauncherWidget::BrtCanShowReticle()
{
    // RebuildWidget 可能先于拥有者给 CrossHairWidget 赋值。识别依据始终是具体控件实例。
    if (IsValid(OwningLauncher) && OwningLauncher->BrtOwnsReticle(this))
    {
        bBrtWasReticle = true;
        BrtReticleOwner = OwningLauncher;
    }
    if (!bBrtWasReticle) return true;
    APBLauncher* Pod = BrtReticleOwner.Get();
    if (!Pod || !Pod->BrtOwnsReticle(this)) return false;
    const APlayerController* PC = GetOwningPlayer();
    // 独立构建的挂载 HUD 有时只先收到自身的 OwningLauncher。
    if (!PC)
    {
        const APawn* Owner = Pod->GetOwnerPawn();
        PC = IsValid(Owner) ? Cast<APlayerController>(Owner->GetController()) : nullptr;
    }
    const APBCharacter* Viewer = PC && PC->IsLocalController() ? Cast<APBCharacter>(PC->GetPawn()) : nullptr;
    return Viewer && Viewer->BrtIsPresentingPod(Pod);
}

void UPBLauncherWidget::BrtRefreshReticleLayout(APBLauncher* Pod)
{
    if (Pod && Pod->BrtOwnsReticle(this))
    {
        bBrtWasReticle = true;
        BrtReticleOwner = Pod;
    }
    const bool bShown = BrtCanShowReticle();
    if (bShown != bBrtLastReticleShown)
    {
        bBrtLastReticleShown = bShown;
        InvalidateLayoutAndVolatility();
    }
}

TSharedRef<SWidget> UPBLauncherWidget::BrtReticleContent(TSharedRef<SWidget> Content)
{
    if (IsDesignTime()) return Content;
    bBrtLastReticleShown = BrtCanShowReticle();
    // 外层独立控制可见性。下次展开继续用原控件的弹量、瞄准模式和鼠标命中规则。
    const TWeakObjectPtr<UPBLauncherWidget> WeakThis(this);
    return SNew(SBox)
        .Visibility_Lambda([WeakThis]()
        {
            UPBLauncherWidget* Self = WeakThis.Get();
            return Self && Self->BrtCanShowReticle()
                ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        [Content];
}

void APBPlayerController::BrtFilterPodView(TSet<FPrimitiveComponentId>& Hidden)
{
    if (!IsLocalController()) return;
    APBCharacter* Character = Cast<APBCharacter>(GetPawn());
    if (Character) Character->BrtRememberLocalPods(BrtLocalPods);
    for (auto Entry = BrtLocalPods.CreateIterator(); Entry; ++Entry)
    {
        APBLauncher* Pod = Entry.Key().Get();
        const APawn* Owner = Pod ? Pod->GetOwnerPawn() : nullptr;
        if (!Pod || (Owner && Owner != Entry.Value().Get()))
        {
            Entry.RemoveCurrent();
            continue;
        }
        Pod->BrtRefreshReticleLayout();
        // 失去 Pawn 的死亡／观战期间仍裁掉先前本机的前景模型。武器销毁时弱引用自动清空。
        if (!Character || !Character->BrtIsPresentingPod(Pod)) Pod->BrtHideFirstPersonPod(Hidden);
    }
}

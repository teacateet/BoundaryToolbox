#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PBHelmetHUD_MiniMap.h"
#include "PBMarkableComponent.h"
#include "PBMiniMapNode.h"
#include "../BoundaryRound.hpp"

// 归本机小地图所有。在小地图内调用，Create 和 AddCanvas 的 lambda 由 HUD 成员函数构造，
// 可合法使用既有的节点类与受保护的 K2_AddWidgetToCanvasPanel。
class FBrtMarkedRadar {
    using View = brt::round::RadarView;
    using Key = TWeakObjectPtr<UPBMarkableComponent>;
    using Node = TWeakObjectPtr<UPBMiniMapNode>;
    brt::round::MarkedRadar<Key, Node> Nodes;
    TWeakObjectPtr<UPBHelmetHUD_MiniMap> HUD;
    View Current{};
    uint64 CaptureSerial = 0;
    bool InCapture = false;
    TSet<Key> EligibleTargets;
    TSet<Key> NativeDrawn;

    struct FSnapshotRange {
        template<class Map, class Visit>
        static void Each(const Map& Snapshot, Visit&& Visitor) {
            for (const auto& Pair : Snapshot) Visitor(Pair.Key, Pair.Value);
        }
    };
    struct FRemove {
        void Remove(Node Widget) const {
            if (UPBMiniMapNode* Live = Widget.Get()) Live->RemoveFromParent();
        }
    };
    struct FSets {
        const TSet<Key>& Allowed;
        const TSet<Key>& Painted;
        bool Eligible(Key Target) const { return Allowed.Contains(Target); }
        bool Drawn(Key Target) const { return Painted.Contains(Target); }
    };
    template<class Create, class AddCanvas>
    struct FWidgets {
        Create& New;
        AddCanvas& Add;
        bool ValidTarget(Key Target) const { return Target.IsValid(); }
        Key WeakTarget(UPBMarkableComponent* Target) const { return Target; }
        Node WeakNode(UPBMiniMapNode* Widget) const { return Widget; }
        UPBMiniMapNode* Resolve(Node Widget) const { return Widget.Get(); }
        UPBMiniMapNode* MakeNode() { return New(); }
        void AddToNativeCanvas(UPBMiniMapNode* Widget) { Add(Widget); }
    };
public:
    void Detach() {
        FRemove Surface;
        Nodes.Detach(Surface);
        HUD.Reset();
        Current = {};
        EligibleTargets.Reset();
        NativeDrawn.Reset();
        CaptureSerial = 0;
        InCapture = false;
    }
    void SetView(UPBHelmetHUD_MiniMap& OwnerHud, View NewView) {
        if (HUD.Get() == &OwnerHud && Current == NewView) return;
        Detach();
        APlayerController* Player = OwnerHud.GetOwningPlayer();
        if (!NewView.life.Valid() || !NewView.view || !Player || !Player->IsLocalController()) return;
        HUD = &OwnerHud;
        Current = NewView;
        FRemove Surface;
        Nodes.Attach(Current, Surface);
    }
    void BeginCapture(View ForView) {
        InCapture = HUD.IsValid() && ForView == Current;
        EligibleTargets.Reset();
        NativeDrawn.Reset();
    }
    // 在原有本方捕获、遮挡/距离及 HUD 显隐规则全部通过后记录；当节点真正绘制后再记录 Drawn。
    void NoteEligible(UPBMarkableComponent* Target) {
        if (InCapture && IsValid(Target)) EligibleTargets.Add(Target);
    }
    void NoteDrawn(UPBMarkableComponent* Target) {
        if (InCapture && IsValid(Target)) NativeDrawn.Add(Target);
    }
    template<class Create, class AddCanvas>
    bool FinishCapture(View ForView, Create&& New, AddCanvas&& Add) {
        if (!InCapture || !HUD.IsValid() || ForView != Current) return false;
        InCapture = false;
        FWidgets<Create, AddCanvas> Factory{New, Add};
        FSets Frame{EligibleTargets, NativeDrawn};
        using SurfaceType = brt::round::NativeRadarSurface<EPBCamp, EPBMarkableType,
            UPBHelmetHUD_MiniMap, UPBMarkableComponent, UPBMiniMapNode, Key, Node,
            FSnapshotRange, decltype(Factory), FSets>;
        SurfaceType Surface(*HUD.Get(), Factory, Frame);
        return Nodes.Finish(Current, ++CaptureSerial,
            [&](auto&& Visit) { Surface.VisitCapture(Visit); }, Surface);
    }
};

#include "BoundaryRound.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include <type_traits>

using namespace SDK;
using namespace brt::round;

using Permission = bool (APBGameMode::*)(APBPlayerController*);
using OwnerPermission = bool (APBPlayerController::*)();
using Open = void (APBPlayerController::*)();
using Confirm = void (APBPlayerController::*)(FName);
using Dispatch = void (APBPlayerController::*)(const FName&);
using Update = void (UPBMiniMapNode::*)(const FPBMiniMapIconInfo&);
using AddCanvas = void (UPBHelmetHUD_MiniMap::*)(UPBMiniMapNode*);
static_assert(std::is_same_v<decltype(&APBGameMode::CanPlayerSelectRole), Permission>);
static_assert(std::is_same_v<decltype(&APBPlayerController::CanSelectRole), OwnerPermission>);
static_assert(std::is_same_v<decltype(&APBPlayerController::OpenInGameSelectRoleMenu), Open>);
static_assert(std::is_same_v<decltype(&APBPlayerController::ConfirmRoleSelection), Confirm>);
static_assert(std::is_same_v<decltype(&APBPlayerController::ServerConfirmRoleSelection), Dispatch>);
static_assert(std::is_same_v<decltype(&UPBMiniMapNode::UpdateNode), Update>);
static_assert(std::is_same_v<decltype(&UPBHelmetHUD_MiniMap::K2_AddWidgetToCanvasPanel), AddCanvas>);
static_assert(std::is_same_v<decltype(APBGameEventManager::ServerKillDetailInfoDelegates),
    TMulticastInlineDelegate<void(float, EPBHitZone, TSubclassOf<UDamageType>, AController*, AController*,
                                 APawn*, APawn*, AActor*)>>);
static_assert(std::is_same_v<decltype(APBGameEventManager::ServerSpawnCharacterDelegate),
    TMulticastInlineDelegate<void(APBPlayerState*)>>);
static_assert(std::is_same_v<decltype(APBGameEventManager::GameRoundEndDelegate),
    TMulticastInlineDelegate<void(APBPlayerState*, EPBGameResult)>>);
static_assert(std::is_same_v<decltype(APBGameEventManager::ServerSensorMarkEventDelegates),
    TMulticastInlineDelegate<void(APBPlayerState*)>>);
using MarkEvent = void (UPBVisionMarkingComponent::*)(UPBDynamicMarkableComponent*);
static_assert(std::is_same_v<decltype(&UPBVisionMarkingComponent::ServerVisionMark), MarkEvent>);
static_assert(std::is_same_v<decltype(&UPBVisionMarkingComponent::ServerVisionUnMark), MarkEvent>);
static_assert(std::is_same_v<decltype(&UPBVisionMarkingComponent::ServerSharedMark), MarkEvent>);
static_assert(std::is_same_v<decltype(&UPBVisionMarkingComponent::ServerSharedUnMark), MarkEvent>);

// 仅做静态语法检查。镜像 SDK 将 TPair 的字段表示为访问函数，故在此转换遍历形式。
struct Range {
    template<class Map, class Each>
    static void EachPair(const Map& source, Each visit) {
        for (const auto& pair : source) visit(pair.Key(), pair.Value());
    }
    template<class Map, class EachFn>
    static void Each(const Map& source, EachFn visit) { EachPair(source, visit); }
};
struct Factory {
    bool ValidTarget(UPBMarkableComponent*) const;
    UPBMarkableComponent* WeakTarget(UPBMarkableComponent*);
    UPBMiniMapNode* WeakNode(UPBMiniMapNode*);
    UPBMiniMapNode* Resolve(UPBMiniMapNode*);
    UPBMiniMapNode* MakeNode();
    void AddToNativeCanvas(UPBMiniMapNode*);
};
struct Capture {
    bool Eligible(UPBMarkableComponent*) const;
    bool Drawn(UPBMarkableComponent*) const;
};
using Surface = NativeRadarSurface<EPBCamp, EPBMarkableType, UPBHelmetHUD_MiniMap, UPBMarkableComponent,
    UPBMiniMapNode, UPBMarkableComponent*, UPBMiniMapNode*, Range, Factory, Capture>;

bool CompileRadar(UPBHelmetHUD_MiniMap& hud, Factory& factory, Capture& frame) {
    Surface surface(hud, factory, frame);
    MarkedRadar<UPBMarkableComponent*, UPBMiniMapNode*> radar;
    RadarView view{{1,1},1};
    radar.Attach(view, surface);
    const bool result = radar.Finish(view, 1, [&](auto visit) { surface.VisitCapture(visit); }, surface);
    radar.Detach(surface);
    return result;
}
bool CompileRound(APBPlayerController& serverPC, APBPlayerController& localPC, APBGameMode& mode,
                  APawn* pawn, FName role) {
    RoundServer<APBPlayerController, APawn*> server(serverPC);
    DeathMenu<APBPlayerController, FName> menu(localPC);
    auto copyToOwner = [&](auto&, const auto& state) { menu.Receive(localPC, state); };
    Life life{1, 1};
    server.Spawn(serverPC, life, pawn, mode, copyToOwner);
    server.FinalDeath(&serverPC, life, pawn, mode, copyToOwner);
    server.Refresh(life, mode, copyToOwner);
    menu.Press(localPC, true, false, false, false);
    menu.ResumePress(localPC, true, false, false, [] {});
    DispatchInGameMenu(localPC, false, false, true, [] { return false; }, [] {});
    bool selected = menu.Confirm(localPC, life, role, true);
    return selected && server.End(1, copyToOwner);
}

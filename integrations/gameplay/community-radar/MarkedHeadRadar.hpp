#pragma once
#include "SparrowReticle.hpp"
#include "UiJournal.hpp"
#include <chrono>
#include <cmath>
#include <optional>

namespace BrtHeadRadar
{
using namespace SDK;
using Weak = BrtSparrowReticle::Instance;
using Hud = UUMG_HelmetHUD_MiniMap_C;
using Dot = UUMG_MinimapNode_C;
using Info = FPBMiniMapIconInfo;
using Clock = std::chrono::steady_clock;

inline bool OnScreen(UPBIndicator* head, UPBMarkableComponent* key, APlayerController* viewer)
{
    if (!UKismetSystemLibrary::IsValid(head) || !UKismetSystemLibrary::IsValid(key) ||
        head->RelatedMarkable != key || head->GetOwningPlayer() != viewer || head->Camp != EPBCamp::Enemy ||
        head->bIsDying || !head->bMarkableIsInView || key->MarkableType != EPBMarkableType::Character) return false;
    if (head->Status != EPBIndicatorStatus::InCircle && head->Status != EPBIndicatorStatus::OnScreenAndOutOfCircle) return false;
    if (head->MarkedStatus != EPBMarkedStatus::SensorMark && head->MarkedStatus != EPBMarkedStatus::ReconMark &&
        head->MarkedStatus != EPBMarkedStatus::VisionMark && head->MarkedStatus != EPBMarkedStatus::RadarMark) return false;
    if (!head->GetParent() && !head->IsInViewport()) return false;
    for (UWidget* item = head; item; item = item->GetParent()) {
        if (!item->IsVisible() || !(item->GetRenderOpacity() > 0)) return false;
    }
    const auto size = USlateBlueprintLibrary::GetLocalSize(head->GetCachedGeometry());
    return size.X > 0 && size.Y > 0 && key->IsNotBlocked();
}

inline const Info* Lookup(const TMap<UPBMarkableComponent*,Info>& source, UPBMarkableComponent* key)
{
    for (const auto& entry : source) if (entry.Key() == key) return &entry.Value();
    return nullptr;
}

inline bool InScope(APBCharacter* owner)
{
    static const bool requested = UKismetSystemLibrary::ParseParam(UKismetSystemLibrary::GetCommandLine(), FString(L"BrtLocalPveUi"));
    if (!requested) return false;
    auto* state = UGameplayStatics::GetGameState(owner);
    if (!UKismetSystemLibrary::IsValid(state) || !state->IsA(APBGameState::StaticClass()) || !state->GameModeClass.Get()) return false;
    const auto name = state->GameModeClass->GetName();
    return name == "BP_PBGameMode_Rush_PVE_Easy_C" || name == "BP_PBGameMode_Rush_PVE_Normal_C" ||
           name == "BP_PBGameMode_Rush_PVE_Hard_C";
}

inline std::optional<Info> CaptureAt(Hud* hud, const FVector& at, EPBCamp camp, UPBMarkableComponent* key, const FVector& observer, APBCharacter* actor)
{
    using BrtUiJournal::Stage;
    const float range = hud->MiniMapWorldHalfRange;
    const auto x = at.X-observer.X, y = at.Y-observer.Y, z = at.Z-observer.Z;
    if (!(std::isfinite(range) && range > 0 && x*x + y*y + z*z <= range*range)) {
        BrtUiJournal::Note(actor,Stage::RangeRejected); return {};
    }
    Info info{};
    const auto* cached = Lookup(hud->MapNodeInfo,key);
    if (!cached) cached = Lookup(hud->LastCaptureInfoNodeMap,key);
    if (cached) {
        if (cached->bIsLocal || cached->Camp != EPBCamp::Enemy || cached->MarkableType != EPBMarkableType::Character) {
            BrtUiJournal::Note(actor,Stage::StyleRejected); return {};
        }
        info = *cached;
    }
    // 只对 OnScreen 已核实的本方可见标记补点。缓存用于保留图标样式，不决定本帧位置或标记状态。
    if (!UKismetSystemLibrary::IsValid(info.Texture)) info.Texture = key->MiniMapTexture;
    if (!UKismetSystemLibrary::IsValid(info.Texture)) { BrtUiJournal::Note(actor,Stage::StyleRejected); return {}; }
    info.Camp = camp;
    info.MarkableType = key->MarkableType;
    info.bMarked = true;
    info.WorldLocation = at;
    info.position = hud->Get2DPositionByLocation(at);
    const auto u = hud->IconPositionUVClamp_U, v = hud->IconPositionUVClamp_V;
    if (!(info.position.X >= u.X && info.position.X <= u.Y && info.position.Y >= v.X && info.position.Y <= v.Y)) {
        BrtUiJournal::Note(actor,Stage::ProjectionRejected); return {};
    }
    return info;
}

inline std::optional<Info> Capture(Hud* hud, const UPBIndicator* head, UPBMarkableComponent* key, const FVector& observer, APBCharacter* actor = nullptr)
{
    return CaptureAt(hud, head->WorldLocation, head->Camp, key, observer, actor);
}

inline bool Covered(Hud* hud, APlayerController* viewer, const Info& info)
{
    for (auto* base : hud->DrewMap) {
        if (!UKismetSystemLibrary::IsValid(base) || !base->IsA(Dot::StaticClass()) ||
            base->GetOwningPlayer() != viewer || base->GetParent() != hud->MiniMapNodeContainer ||
            !base->IsVisible() || !(base->GetRenderOpacity() > 0)) continue;
        bool pooled = false;
        for (auto* unused : hud->NotUsedWidgetArray) if (unused == base) { pooled = true; break; }
        if (pooled) continue;
        const auto& stock = static_cast<Dot*>(base)->MiniMapIconInfo;
        if (stock.bMarked && !stock.bIsLocal && stock.Camp == info.Camp && stock.MarkableType == info.MarkableType &&
            stock.Texture == info.Texture && std::abs(stock.position.X - info.position.X) < 0.001f &&
            std::abs(stock.position.Y - info.position.Y) < 0.001f) return true;
    }
    return false;
}

struct Extra {
    Weak target, node, panel;
    bool kept = false;
    void Remove() const {
        auto* dot = static_cast<Dot*>(node.Resolve());
        if (dot && dot->GetParent() && dot->GetParent() == panel.Resolve()) dot->RemoveFromParent();
    }
};

struct View {
    std::optional<Weak> map;
    std::optional<Weak> pawn;
    std::vector<Extra> extras;
    Clock::time_point next{}, search{};
    bool deathEvent = false, sawDeadState = false;

    void Clear() {
        for (auto& extra : extras) extra.Remove();
        extras.clear();
        map.reset();
        pawn.reset();
        next = search = {};
        deathEvent = sawDeadState = false;
    }

    Hud* Find(APBCharacter* actor, APlayerController* pc, Clock::time_point now) {
        auto* hud = map ? static_cast<Hud*>(map->Resolve()) : nullptr;
        const auto usable = [&](Hud* value) {
            return value && value->OnwerCharacter == actor && value->GetOwningPlayer() == pc &&
                   UKismetSystemLibrary::IsValid(value->MiniMapNodeContainer) && value->MiniMapNodeContainer->IsVisible() &&
                   value->IsVisible();
        };
        if (usable(hud)) return hud;
        if (now < search) return nullptr;
        search = now + std::chrono::seconds(1);
        TArray<UUserWidget*> found;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(actor,&found,Hud::StaticClass(),false);
        for (auto* candidate : found) {
            auto* value = static_cast<Hud*>(candidate);
            if (UKismetSystemLibrary::IsValid(value) && usable(value)) { map.emplace(value); return value; }
        }
        return nullptr;
    }

    void Draw(Hud* hud, APlayerController* pc, UPBMarkableComponent* target, const Info& info, APBCharacter* actor = nullptr) {
        using BrtUiJournal::Stage;
        if (Covered(hud,pc,info)) { BrtUiJournal::Note(actor,Stage::NativeDot); return; }
        auto it = std::find_if(extras.begin(),extras.end(),[&](const Extra& e) {
            return e.target.Resolve() == target && e.panel.Resolve() == hud->MiniMapNodeContainer;
        });
        if (it != extras.end() && it->kept) return;
        auto* dot = it != extras.end() ? static_cast<Dot*>(it->node.Resolve()) : nullptr;
        if (!dot || dot->GetParent() != hud->MiniMapNodeContainer) {
            if (it != extras.end()) { it->Remove(); extras.erase(it); }
            auto* created = UWidgetBlueprintLibrary::Create(hud,hud->UPBMiniMapNodeClass,pc);
            if (!UKismetSystemLibrary::IsValid(created) || !created->IsA(Dot::StaticClass()) || created->GetOwningPlayer() != pc) {
                BrtUiJournal::Note(actor,Stage::NodeRejected); return;
            }
            dot = static_cast<Dot*>(created);
            hud->K2_AddWidgetToCanvasPanel(dot);
            if (dot->GetParent() != hud->MiniMapNodeContainer) { BrtUiJournal::Note(actor,Stage::NodeRejected); return; }
            extras.push_back({Weak(target),Weak(dot),Weak(hud->MiniMapNodeContainer)});
            it = std::prev(extras.end());
        }
        dot->UpdateNode(info);
        BrtUiJournal::Note(actor,Stage::DrewDot);
        it->kept = true;
    }

    void Pass(APBCharacter* actor, APlayerController* pc, Hud* hud, UPBMarkerManager* marks) {
        for (auto& extra : extras) extra.kept = false;
        if (hud && marks) {
            BrtUiJournal::Note(actor,BrtUiJournal::Stage::Scan);
            const auto observer = actor->K2_GetActorLocation();
            const auto visit = [&](const auto& shown) {
                for (const auto& item : shown) {
                    auto* target = item.Key();
                    auto* head = item.Value();
                    if (!OnScreen(head,target,pc)) continue;
                    BrtUiJournal::Note(actor,BrtUiJournal::Stage::Visible);
                    if (auto info = Capture(hud,head,target,observer,actor)) Draw(hud,pc,target,*info,actor);
                }
            };
            visit(marks->IndicatorMap);
            visit(marks->VisionMarkedMap);
            visit(marks->ForceMarkedMap);
            // 本机专服可先复制标记及其归属，之后才创建头顶控件。只接纳本方本帧可见的标记。
            const auto mine = [&](const auto& sources) {
                for (auto* source : sources)
                    if (UKismetSystemLibrary::IsValid(source) && (source == pc->PlayerState ||
                        (actor->TeamID >= 0 && actor->TeamID < 2 && source->GetTeamId() == actor->TeamID))) return true;
                return false;
            };
            const auto visitConfirmed = [&](const auto& list) {
                for (auto* key : list) {
                    if (!UKismetSystemLibrary::IsValid(key) || key->MarkableType != EPBMarkableType::Character ||
                        !((key->bMarked && mine(key->MarkCounterArray)) ||
                          (key->bSharedMarked && mine(key->SharedMarkCounterArray)) ||
                          (key->bMarkedBySensor && mine(key->SensorMarkCounterArray)))) continue;
                    auto* owner = key->GetOwner();
                    if (!UKismetSystemLibrary::IsValid(owner) || !owner->IsA(APBCharacter::StaticClass())) continue;
                    auto* enemy = static_cast<APBCharacter*>(owner);
                    if (!enemy->IsAlive() || enemy->IsInKilledStatus() || enemy->TeamID < 0 || enemy->TeamID > 1 ||
                        actor->TeamID < 0 || actor->TeamID > 1 || enemy->TeamID == actor->TeamID || !key->IsNotBlocked()) continue;
                    int width{}, height{};
                    pc->GetViewportSize(&width, &height);
                    const auto at = enemy->K2_GetActorLocation();
                    FVector2D screen{};
                    if (!pc->ProjectWorldLocationToScreen(at, &screen, true) ||
                        !(screen.X >= 0 && screen.X < width && screen.Y >= 0 && screen.Y < height)) continue;
                    BrtUiJournal::Note(actor,BrtUiJournal::Stage::Visible);
                    if (auto info = CaptureAt(hud, at, EPBCamp::Enemy, key, observer, actor)) Draw(hud,pc,key,*info,actor);
                }
            };
            visitConfirmed(marks->MarkedArray);
            visitConfirmed(marks->SharedMarkedArray);
            visitConfirmed(marks->SensorMarkedArray);
        }
        std::erase_if(extras,[](const Extra& e){ if (e.kept) return false; e.Remove(); return true; });
    }
};

inline void OnCharacter(APBCharacter* actor, std::string_view name)
{
    static View view;
    if (!actor) return;
    if (BrtSparrowReticle::Event(name,"NotifyBeKilled")) {
        if (view.pawn && view.pawn->Resolve() == actor) {
            view.Pass(actor,nullptr,nullptr,nullptr);
            view.deathEvent = true;
            view.sawDeadState = false;
        }
        return;
    }
    if (BrtSparrowReticle::Event(name,"ReceiveUnpossessed") ||
        BrtSparrowReticle::Event(name,"ReceiveEndPlay")) {
        BrtUiJournal::Unbind(actor);
        if (view.pawn && view.pawn->Resolve() == actor) view.Clear();
        return;
    }
    if (!BrtSparrowReticle::Event(name,"ReceiveTick")) return;
    auto* ctrl = actor->GetController();
    if (!ctrl || !ctrl->IsLocalPlayerController() || ctrl->K2_GetPawn() != actor) {
        BrtUiJournal::Unbind(actor);
        if (view.pawn && view.pawn->Resolve() == actor) view.Clear();
        return;
    }
    auto* pc = static_cast<APlayerController*>(ctrl);
    if (view.pawn && view.pawn->Resolve() != actor) view.Clear();
    if (!view.pawn) view.pawn.emplace(actor);
    auto now = Clock::now();
    if (now < view.next) return;
    view.next = now + std::chrono::milliseconds(125);
    const bool living = actor->IsAlive() && !actor->IsInKilledStatus();
    if (view.deathEvent && !living) view.sawDeadState = true;
    if (view.sawDeadState && living) view.sawDeadState = view.deathEvent = false;
    const bool scoped = InScope(actor);
    if (scoped) BrtUiJournal::Bind(actor,[]{return UKismetSystemLibrary::GetProjectSavedDirectory().ToWString();});
    else BrtUiJournal::Unbind(actor);
    if (!scoped || !living || view.deathEvent) {
        view.Pass(actor,pc,nullptr,nullptr);
        return;
    }
    if (!actor->IsInNormalRadarSignal()) {
        BrtUiJournal::Note(actor,BrtUiJournal::Stage::NoSignal);
        view.Pass(actor,pc,nullptr,nullptr); return;
    }
    auto* hud = view.Find(actor,pc,now);
    auto* subsystem = hud ? USubsystemBlueprintLibrary::GetWorldSubsystem(actor,UPBMarkerManager::StaticClass()) : nullptr;
    auto* marks = UKismetSystemLibrary::IsValid(subsystem) ? static_cast<UPBMarkerManager*>(subsystem) : nullptr;
    if (!hud) BrtUiJournal::Note(actor,BrtUiJournal::Stage::NoHud);
    else if (!marks) BrtUiJournal::Note(actor,BrtUiJournal::Stage::NoManager);
    view.Pass(actor,pc,hud,marks);
}
}

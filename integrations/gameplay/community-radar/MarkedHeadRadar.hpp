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

inline bool VisibleIndicator(UPBIndicator* head, UPBMarkableComponent* key, APlayerController* viewer)
{
    if (!UKismetSystemLibrary::IsValid(head) || !UKismetSystemLibrary::IsValid(key) ||
        head->RelatedMarkable != key || head->GetOwningPlayer() != viewer || head->bIsDying ||
        (!head->GetParent() && !head->IsInViewport())) return false;
    for (UWidget* item = head; item; item = item->GetParent()) {
        if (!item->IsVisible() || !(item->GetRenderOpacity() > 0)) return false;
    }
    const auto size = USlateBlueprintLibrary::GetLocalSize(head->GetCachedGeometry());
    return size.X > 0 && size.Y > 0;
}

inline bool OnScreen(UPBIndicator* head, UPBMarkableComponent* key, APlayerController* viewer)
{
    if (!UKismetSystemLibrary::IsValid(head) || !UKismetSystemLibrary::IsValid(key) ||
        head->Camp != EPBCamp::Enemy || !head->bMarkableIsInView || key->MarkableType != EPBMarkableType::Character) return false;
    if (head->Status != EPBIndicatorStatus::InCircle && head->Status != EPBIndicatorStatus::OnScreenAndOutOfCircle) return false;
    if (head->MarkedStatus != EPBMarkedStatus::SensorMark && head->MarkedStatus != EPBMarkedStatus::ReconMark &&
        head->MarkedStatus != EPBMarkedStatus::VisionMark && head->MarkedStatus != EPBMarkedStatus::RadarMark) return false;
    return VisibleIndicator(head,key,viewer) && key->IsNotBlocked();
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

inline bool InRange(Hud* hud, const FVector& at, const FVector& observer, APBCharacter* actor)
{
    const float range = hud->MiniMapWorldHalfRange;
    const auto x = at.X-observer.X, y = at.Y-observer.Y, z = at.Z-observer.Z;
    if (!(std::isfinite(range) && range > 0 && x*x + y*y + z*z <= range*range)) {
        BrtUiJournal::Note(actor,BrtUiJournal::Stage::RangeRejected); return false;
    }
    return true;
}

inline std::optional<Info> Project(Hud* hud, Info info, const FVector& at, APBCharacter* actor)
{
    info.WorldLocation = at;
    info.position = hud->Get2DPositionByLocation(at);
    const auto u = hud->IconPositionUVClamp_U, v = hud->IconPositionUVClamp_V;
    if (!(info.position.X >= u.X && info.position.X <= u.Y && info.position.Y >= v.X && info.position.Y <= v.Y)) {
        BrtUiJournal::Note(actor,BrtUiJournal::Stage::ProjectionRejected); return {};
    }
    return info;
}

inline std::optional<Info> CaptureAt(Hud* hud, const FVector& at, EPBCamp camp, UPBMarkableComponent* key, const FVector& observer, APBCharacter* actor)
{
    using BrtUiJournal::Stage;
    if (!InRange(hud,at,observer,actor)) return {};
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
    return Project(hud,info,at,actor);
}

inline std::optional<Info> SupplyIcon(Hud* hud, UPBMarkableComponent* key, const FVector& observer, APBCharacter* actor)
{
    if (!UKismetSystemLibrary::IsValid(key) || !key->bLocalMarked || key->MarkableType != EPBMarkableType::InteractiveObj ||
        !UKismetSystemLibrary::IsValid(key->MiniMapTexture)) return {};
    auto* box = key->GetOwner();
    if (!UKismetSystemLibrary::IsValid(box) || box->bHidden || !box->IsA(APBSupplyPoint::StaticClass())) return {};
    auto* room = UGameplayStatics::GetGameState(actor);
    if (!room || UGameplayStatics::GetGameState(box) != room) return {};
    BrtUiJournal::Note(actor,BrtUiJournal::Stage::SupplyLocal);
    const auto at = key->GetLocation();
    if (!InRange(hud,at,observer,actor)) return {};
    Info info{};
    info.Camp = EPBCamp::Neutral;
    const auto* stock = Lookup(hud->MapNodeInfo,key);
    if (!stock) stock = Lookup(hud->LastCaptureInfoNodeMap,key);
    if (stock && !stock->bIsLocal && stock->MarkableType == key->MarkableType && stock->Texture == key->MiniMapTexture)
        info = *stock;
    info.MarkableType = key->MarkableType;
    info.Texture = key->MiniMapTexture;
    info.bMarked = true;
    return Project(hud,info,at,actor);
}

inline std::optional<Info> Capture(Hud* hud, const UPBIndicator* head, UPBMarkableComponent* key, const FVector& observer, APBCharacter* actor = nullptr)
{
    return CaptureAt(hud, head->WorldLocation, head->Camp, key, observer, actor);
}

inline APBCharacter* LivingEnemy(APBCharacter* actor, AActor* target)
{
    if (!UKismetSystemLibrary::IsValid(target) || target == actor || !target->IsA(APBCharacter::StaticClass())) return nullptr;
    auto* enemy = static_cast<APBCharacter*>(target);
    if (actor->TeamID < 0 || actor->TeamID > 1 || enemy->TeamID < 0 || enemy->TeamID > 1 ||
        enemy->TeamID == actor->TeamID || !enemy->IsAlive() || enemy->IsInKilledStatus()) return nullptr;
    return enemy;
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
        if (stock.bIsLocal || stock.MarkableType != info.MarkableType || stock.Texture != info.Texture) continue;
        // 弹药箱是静止的本地标记。转身时原生点和补点可能相差一帧投影；
        // 按实物位置交接，不能用对敌侦察的 bMarked 标志判断原生点是否可见。
        if (info.MarkableType == EPBMarkableType::InteractiveObj) {
            if (std::abs(stock.WorldLocation.X-info.WorldLocation.X) <= 1.f &&
                std::abs(stock.WorldLocation.Y-info.WorldLocation.Y) <= 1.f &&
                std::abs(stock.WorldLocation.Z-info.WorldLocation.Z) <= 1.f) return true;
        } else if (stock.bMarked && stock.Camp == info.Camp && std::abs(stock.position.X - info.position.X) < 0.001f &&
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
        const bool supply = info.MarkableType == EPBMarkableType::InteractiveObj;
        if (Covered(hud,pc,info)) { BrtUiJournal::Note(actor,supply ? Stage::SupplyNative : Stage::NativeDot); return; }
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
        BrtUiJournal::Note(actor,supply ? Stage::SupplyDrawn : Stage::DrewDot);
        it->kept = true;
    }

    void Pass(APBCharacter* actor, APlayerController* pc, Hud* hud, UPBMarkerManager* marks) {
        for (auto& extra : extras) extra.kept = false;
        if (hud && marks) {
            BrtUiJournal::Note(actor,BrtUiJournal::Stage::Scan);
            const auto observer = actor->K2_GetActorLocation();
            // 零售弹药箱发布的是静态本地标记：不扫描世界 Actor，也不借用敌人的侦察名单。
            const auto supply = [&](UPBMarkableComponent* key) {
                if (auto info = SupplyIcon(hud,key,observer,actor)) Draw(hud,pc,key,*info,actor);
            };
            for (auto* key : marks->LocalMarkedArray) supply(key);
            const auto visit = [&](const auto& shown) {
                for (const auto& item : shown) {
                    auto* target = item.Key();
                    auto* head = item.Value();
                    if (UKismetSystemLibrary::IsValid(target) && target->MarkableType == EPBMarkableType::InteractiveObj) {
                        if (VisibleIndicator(head,target,pc)) supply(target);
                        continue;
                    }
                    if (!OnScreen(head,target,pc)) continue;
                    BrtUiJournal::Note(actor,BrtUiJournal::Stage::Visible);
                    if (auto info = Capture(hud,head,target,observer,actor)) Draw(hud,pc,target,*info,actor);
                }
            };
            visit(marks->IndicatorMap);
            visit(marks->VisionMarkedMap);
            visit(marks->ForceMarkedMap);
            // 腰射按标记键会生成单独的 MarkActor；墙上的位置点也走这条路，但没有敌方角色目标。
            if (actor->IsA(APBCharacter_BP_C::StaticClass())) {
                auto* mark = static_cast<APBCharacter_BP_C*>(actor)->Mark;
                if (UKismetSystemLibrary::IsValid(mark) &&
                    mark->MarkResult.MarkOwner_3_DD6BB39548A6E38B8E9D27BD82A51719 == actor) {
                    auto* enemy = LivingEnemy(actor,mark->MarkResult.MarkTarget_5_1C9EF1EB484D46D6C0CF7BA74F365E89);
                    auto* key = enemy ? enemy->MarkableComponent : nullptr;
                    if (UKismetSystemLibrary::IsValid(key) && key->GetOwner() == enemy && key->MarkableType == EPBMarkableType::Character) {
                        if (auto info = CaptureAt(hud,enemy->K2_GetActorLocation(),EPBCamp::Enemy,key,observer,actor))
                            Draw(hud,pc,key,*info,actor);
                    }
                }
            }
            // 本方已确认的侦察信号按游戏自己的名单失效；雷达不再重复主画面的视野判定。
            const auto mine = [&](const auto& sources) {
                for (auto* source : sources)
                    if (UKismetSystemLibrary::IsValid(source) && (source == pc->PlayerState ||
                        (actor->TeamID >= 0 && actor->TeamID < 2 && source->GetTeamId() == actor->TeamID))) return true;
                return false;
            };
            const auto visitConfirmed = [&](const auto& list, const auto& confirmed) {
                for (auto* key : list) {
                    if (!UKismetSystemLibrary::IsValid(key) || key->MarkableType != EPBMarkableType::Character || !confirmed(key)) continue;
                    auto* enemy = LivingEnemy(actor,key->GetOwner());
                    if (!enemy || enemy->MarkableComponent != key) continue;
                    const auto at = enemy->K2_GetActorLocation();
                    if (auto info = CaptureAt(hud, at, EPBCamp::Enemy, key, observer, actor)) Draw(hud,pc,key,*info,actor);
                }
            };
            visitConfirmed(marks->MarkedArray,[&](auto* key) { return key->bMarked && mine(key->MarkCounterArray); });
            visitConfirmed(marks->SharedMarkedArray,[&](auto* key) { return key->bSharedMarked && mine(key->SharedMarkCounterArray); });
            visitConfirmed(marks->SensorMarkedArray,[&](auto* key) { return key->bMarkedBySensor && mine(key->SensorMarkCounterArray); });
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

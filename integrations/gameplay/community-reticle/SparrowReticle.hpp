#pragma once
#include "UiJournal.hpp"
#include "ShoulderSlots.hpp"
#include <algorithm>
#include <string_view>
#include <vector>

namespace BrtSparrowReticle
{
using namespace SDK;

// 由引擎解析原有实例。在同名 Widget 重建后检查代次，避免修改替代者。
struct Instance
{
    TSoftObjectPtr<UObject> object;
    explicit Instance(UObject* source) : object(UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(source)) {}
    UObject* Resolve() const
    {
        if (object.WeakPtr.ObjectSerialNumber <= 0) return nullptr;
        auto* value = UKismetSystemLibrary::Conv_SoftObjectReferenceToObject(object);
        if (!UKismetSystemLibrary::IsValid(value)) return nullptr;
        const auto current = UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(value);
        const auto& a = object.WeakPtr;
        const auto& b = current.WeakPtr;
        return a.ObjectIndex == b.ObjectIndex && a.ObjectSerialNumber == b.ObjectSerialNumber ? value : nullptr;
    }
};

struct Entry
{
    Instance widget, panel;
    APBCharacter* owner;
    APlayerController* viewer;
    bool awaitingOwner;
    bool closed = false, sawDeath = false, dimmed = false;
    float original = 0;

    void Paint(UCanvasPanel* canvas, bool show)
    {
        if (show && !dimmed) return;
        const float opacity = canvas->GetRenderOpacity();
        if (show) {
            if (dimmed && opacity == 0.0f) {
                canvas->SetRenderOpacity(original);
                BrtUiJournal::Note(owner,BrtUiJournal::Stage::ReticleRestored);
            }
            dimmed = false;
        } else if (opacity != 0.0f) {
            original = opacity;
            dimmed = true;
            canvas->SetRenderOpacity(0.0f);
            BrtUiJournal::Note(owner,BrtUiJournal::Stage::ReticleHidden);
        }
    }
};

inline bool Event(std::string_view name, std::string_view leaf)
{
    return name == leaf || (name.size() > leaf.size() && name[name.size()-leaf.size()-1] == '.' && name.ends_with(leaf));
}

inline void OnCharacter(APBCharacter* actor, std::string_view name)
{
    const bool death = Event(name,"NotifyBeKilled");
    const bool closed = death || Event(name,"ReceiveUnpossessed") || Event(name,"ReceiveEndPlay");
    const bool possessed = Event(name,"ReceivePossessed") || Event(name,"K2_OnControllerReplicatedPossess");
    if (!closed && !possessed && !Event(name,"ReceiveTick")) return;
    if (!actor) return;

    static std::vector<Entry> entries;
    auto* ctrl = actor->GetController();
    auto* local = ctrl && ctrl->IsLocalPlayerController() ? static_cast<APlayerController*>(ctrl) : nullptr;
    const bool ownsPawn = local && local->K2_GetPawn() == actor;
    if (!ownsPawn && !closed && std::none_of(entries.begin(),entries.end(),[&](const Entry& e){ return e.owner == actor; })) return;
    if (ownsPawn && possessed) {
        std::erase_if(entries,[](const Entry& e){ return !e.widget.Resolve() || !e.panel.Resolve(); });
    }

    auto record = [&](APBLauncher* pod) {
        if (!pod || !UKismetSystemLibrary::IsValid(pod)) return;
        auto* base = pod->CrossHairWidget;
        if (!base || !UKismetSystemLibrary::IsValid(base) || base->OwningLauncher != pod ||
            !base->IsA(UUMG_CrossHair_Missile_C::StaticClass())) return;
        auto* w = static_cast<UUMG_CrossHair_Missile_C*>(base);
        auto* canvas = w->CanvasPanel_0;
        if (!UKismetSystemLibrary::IsValid(canvas)) return;
        auto* widgetViewer = w->GetOwningPlayer();
        auto* podOwner = pod->GetOwnerPawn();
        // 首帧肩位和控件可先于挂载归属同步。只有本地当前角色及其明确视口可暂时收起瞄环。
        if (podOwner ? podOwner != actor : (!ownsPawn || widgetViewer != local)) return;
        // 死亡回调可能晚于解除控制。只有原本地角色及控件自己的本地视口可补登记。
        auto* view = local;
        if (!ownsPawn) {
            if (!closed || !widgetViewer || !widgetViewer->IsLocalPlayerController() ||
                !actor->GetIsLocalControlledBeforeDie()) return;
            view = widgetViewer;
        }
        if (widgetViewer && widgetViewer != view) return;
        BrtUiJournal::Note(actor,BrtUiJournal::Stage::ReticleFound);
        auto found = std::find_if(entries.begin(),entries.end(),[&](const Entry& e) { return e.widget.Resolve() == w && e.panel.Resolve() == canvas; });
        if (found == entries.end()) entries.push_back({Instance(w),Instance(canvas),actor,view,!podOwner});
        else if (found->owner != actor || found->viewer != view) {
            found->owner = actor;
            found->viewer = view;
            found->awaitingOwner = !podOwner;
            found->closed = found->sawDeath = false;
        } else if (podOwner) found->awaitingOwner = false;
    };
    const auto slots = BrtShoulders::Of(actor);
    if (ownsPawn || closed) {
        record(slots.left);
        if (slots.right != slots.left) record(slots.right);
    }

    for (auto it = entries.begin(); it != entries.end();) {
        if (it->owner != actor && (!ownsPawn || it->viewer != local)) { ++it; continue; }
        auto* w = static_cast<UUMG_CrossHair_Missile_C*>(it->widget.Resolve());
        auto* canvas = static_cast<UCanvasPanel*>(it->panel.Resolve());
        if (!w || !canvas) { it = entries.erase(it); continue; }
        auto* widgetViewer = w->GetOwningPlayer();
        auto* pod = w->OwningLauncher;
        const bool validPod = UKismetSystemLibrary::IsValid(pod);
        auto* podOwner = validPod ? pod->GetOwnerPawn() : nullptr;
        if (w->CanvasPanel_0 != canvas || (it->awaitingOwner && !widgetViewer) ||
            (widgetViewer && (widgetViewer != it->viewer || !widgetViewer->IsLocalPlayerController())) ||
            (podOwner && podOwner != it->owner && pod->CrossHairWidget == w)) {
            it->Paint(canvas,true);
            it = entries.erase(it);
            continue;
        }
        const bool same = actor == it->owner;
        const bool living = same && actor->IsAlive() && !actor->IsInKilledStatus();
        if (same && closed) it->closed = true;
        if (same && !living) it->sawDeath = true;
        if (same && ownsPawn && living && !closed && (possessed || it->sawDeath)) {
            it->closed = it->sawDeath = false;
        }
        const bool equipped = same && ownsPawn && living && !it->closed && validPod && podOwner == actor &&
            pod->CrossHairWidget == w && pod == actor->CurrentLauncher &&
            (pod == slots.left || pod == slots.right);
        const bool hands = actor->CurrentHandsState == EPBHandsState::DeployingLauncherController ||
                           actor->CurrentHandsState == EPBHandsState::DeployedLauncherController;
        const bool active = equipped && hands && pod->CurrentState != EPBLauncherState::Standby &&
            pod->CurrentState != EPBLauncherState::Undeploying && pod->PendingState != EPBLauncherState::Undeploying;
        it->Paint(canvas,active);
        ++it;
    }
}
}

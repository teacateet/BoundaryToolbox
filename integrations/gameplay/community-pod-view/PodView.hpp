#pragma once
#include "UiJournal.hpp"
#include "ShoulderSlots.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace BrtPodView
{
using namespace SDK;

inline bool SameGeneration(const FWeakObjectPtr& a, const FWeakObjectPtr& b)
{
    return a.ObjectSerialNumber > 0 && a.ObjectIndex == b.ObjectIndex && a.ObjectSerialNumber == b.ObjectSerialNumber;
}

struct Instance
{
    UObject* address;
    TSoftObjectPtr<UObject> reference;
    explicit Instance(UObject* value) : address(value), reference(UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(value)) {}
    UObject* Resolve() const
    {
        if (reference.WeakPtr.ObjectSerialNumber <= 0) return nullptr;
        auto* value = UKismetSystemLibrary::Conv_SoftObjectReferenceToObject(reference);
        if (value != address || !UKismetSystemLibrary::IsValid(value)) return nullptr;
        const auto current = UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(value);
        return SameGeneration(reference.WeakPtr, current.WeakPtr) ? value : nullptr;
    }
};

struct Pod
{
    Instance launcher, owner;
    bool closed = false, sawDeath = false, awaitingOwner = false;
};
struct View
{
    Instance controller;
    std::vector<Pod> pods;
    std::vector<TWeakObjectPtr<UPrimitiveComponent>> inserted;
};
inline std::vector<View> views;

inline bool Event(std::string_view name, std::string_view tail)
{
    return name == tail || (name.size() > tail.size() && name[name.size()-tail.size()-1] == '.' && name.ends_with(tail));
}

inline bool IsPresenting(APBCharacter* owner, APBLauncher* pod)
{
    if (!BrtShoulders::Of(owner).Contains(pod)) return false;
    if (pod->CurrentState == EPBLauncherState::Undeploying || pod->PendingState == EPBLauncherState::Undeploying) return false;
    const bool selected = pod == owner->CurrentLauncher;
    if (pod->IsA(APBLauncher_Deploy::StaticClass()))
    {
        const auto hands = owner->CurrentHandsState;
        return selected && pod->CurrentState != EPBLauncherState::Standby &&
            (hands == EPBHandsState::DeployingLauncherController || hands == EPBHandsState::DeployedLauncherController);
    }
    if (pod->bIsFiring || pod->bPendingFiring || pod->bInProjectileControlMode || pod->bInSpecialMode) return true;
    if (pod->CurrentState == EPBLauncherState::Deploying || pod->CurrentState == EPBLauncherState::Handup) return true;
    return (selected || pod->IsActivated()) &&
        (pod->CurrentState == EPBLauncherState::Ready || pod->CurrentState == EPBLauncherState::Reloading);
}

inline void Collect(APBLauncher* pod, USceneComponent* node, std::vector<USceneComponent*>& visited,
    std::vector<UPrimitiveComponent*>& hidden)
{
    if (!node || node == pod->ArmMesh3P || node == pod->Mesh3P ||
        std::find(visited.begin(),visited.end(),node) != visited.end() ||
        !UKismetSystemLibrary::IsValid(node) || node->GetOwner() != pod) return;
    visited.push_back(node);
    if (node->IsA(UPrimitiveComponent::StaticClass()))
        hidden.push_back(static_cast<UPrimitiveComponent*>(node));
    const int count = node->GetNumChildrenComponents();
    for (int i = 0; i < count; ++i)
        Collect(pod, node->GetChildComponent(i), visited, hidden);
}

inline void Remember(View& view, APBCharacter* owner, APBLauncher* launcher, bool ownsView)
{
    if (!launcher || !UKismetSystemLibrary::IsValid(launcher)) return;
    auto* actual = launcher->GetOwnerPawn();
    if (ownsView) BrtUiJournal::Note(owner,actual == owner ? BrtUiJournal::Stage::HasOwner :
        (actual ? BrtUiJournal::Stage::OwnerDifferent : BrtUiJournal::Stage::OwnerPending));
    // 首帧的肩位可以早于拥有者同步；只有当前相机正控制的角色才能临时认领该肩位。
    if (actual ? actual != owner : !ownsView) return;
    auto entry = std::find_if(view.pods.begin(), view.pods.end(), [&](const Pod& p) {
        return p.launcher.address == launcher && p.launcher.Resolve() == launcher;
    });
    if (entry == view.pods.end())
        view.pods.push_back({Instance(launcher), Instance(owner), false, false, !actual});
    else if (entry->owner.Resolve() != owner)
        *entry = {Instance(launcher), Instance(owner), false, false, !actual};
}

inline int Row(APlayerController* pc, const FWeakObjectPtr& id)
{
    for (int i = 0; i < pc->HiddenPrimitiveComponents.Num(); ++i)
        if (SameGeneration(pc->HiddenPrimitiveComponents[i], id)) return i;
    return -1;
}

inline void Reconcile(View& view, APlayerController* pc, const std::vector<UPrimitiveComponent*>& hidden)
{
    std::vector<TWeakObjectPtr<UPrimitiveComponent>> wanted;
    for (auto* mesh : hidden)
    {
        const auto reference = UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(mesh);
        if (reference.WeakPtr.ObjectSerialNumber <= 0) continue;
        TWeakObjectPtr<UPrimitiveComponent> weak{};
        static_cast<FWeakObjectPtr&>(weak) = reference.WeakPtr;
        wanted.push_back(weak);
        if (Row(pc, weak) >= 0) continue;
        pc->HiddenPrimitiveComponents.Add(weak);
        if (std::none_of(view.inserted.begin(), view.inserted.end(), [&](const auto& old){ return SameGeneration(old,weak); }))
            view.inserted.push_back(weak);
    }
    std::erase_if(view.inserted, [&](const auto& old) {
        if (std::any_of(wanted.begin(), wanted.end(), [&](const auto& live){ return SameGeneration(live,old); })) return false;
        const int at = Row(pc, old);
        if (at >= 0) pc->HiddenPrimitiveComponents.Remove(at);
        return true;
    });
}

inline void Update(View& view, APlayerController* pc, APBCharacter* actor, bool close, bool possess)
{
    auto* localPawn = pc->K2_GetPawn();
    std::vector<USceneComponent*> visited;
    std::vector<UPrimitiveComponent*> hidden;
    std::erase_if(view.pods, [&](Pod& record) {
        auto* pod = static_cast<APBLauncher*>(record.launcher.Resolve());
        if (!pod) return true;
        auto* owner = static_cast<APBCharacter*>(record.owner.Resolve());
        auto* actual = pod->GetOwnerPawn();
        if (actual && actual != owner) return true;
        if (record.awaitingOwner)
        {
            if (!owner || localPawn != owner || !BrtShoulders::Of(owner).Contains(pod)) return true;
            if (actual) record.awaitingOwner = false;
        }
        const bool alive = owner && localPawn == owner && owner->IsAlive() && !owner->IsInKilledStatus();
        if (owner == actor)
        {
            if (close) record.closed = true;
            if (owner && (!owner->IsAlive() || owner->IsInKilledStatus())) record.sawDeath = true;
            if (!close && alive && (possess || record.sawDeath)) record.closed = record.sawDeath = false;
        }
        if (!alive || record.closed || !actual || !IsPresenting(owner,pod))
        {
            const auto previous = hidden.size();
            Collect(pod, pod->ArmMesh1P, visited, hidden);
            Collect(pod, pod->Mesh1P, visited, hidden);
            if (owner == actor) BrtUiJournal::Note(actor,hidden.size() != previous ?
                BrtUiJournal::Stage::HidePrimitives : BrtUiJournal::Stage::HideEmpty);
        }
        else if (owner == actor) BrtUiJournal::Note(actor,BrtUiJournal::Stage::Presenting);
        return false;
    });
    Reconcile(view, pc, hidden);
}

inline void OnCharacter(APBCharacter* actor, std::string_view name)
{
    const bool close = Event(name,"NotifyBeKilled") || Event(name,"ReceiveUnpossessed") || Event(name,"ReceiveEndPlay");
    const bool possess = Event(name,"ReceivePossessed") || Event(name,"K2_OnControllerReplicatedPossess");
    if (!actor || (!close && !possess && !Event(name,"ReceiveTick"))) return;
    auto* control = actor->GetController();
    auto* local = control && control->IsLocalPlayerController() ? static_cast<APlayerController*>(control) : nullptr;
    const bool owns = local && local->K2_GetPawn() == actor;
    if (!owns) local = nullptr;
    if (!local && close && actor->GetIsLocalControlledBeforeDie())
    {
        auto* prior = UGameplayStatics::GetPlayerController(actor, 0);
        if (prior && prior->IsLocalPlayerController() && (!prior->K2_GetPawn() || prior->K2_GetPawn() == actor))
            local = prior;
    }
    if (local && std::none_of(views.begin(), views.end(), [&](const View& v) {
            return v.controller.address == local && v.controller.Resolve() == local;
        }))
        views.push_back({Instance(local),{},{}});
    const auto slots = BrtShoulders::Of(actor);
    for (auto it = views.begin(); it != views.end();)
    {
        const bool thisViewer = local && it->controller.address == local;
        const bool previousOwner = std::any_of(it->pods.begin(),it->pods.end(),[&](const Pod& p) {
            return p.owner.address == actor || p.launcher.address == slots.left || p.launcher.address == slots.right;
        });
        if (!thisViewer && !previousOwner) { ++it; continue; }
        auto* pc = static_cast<APlayerController*>(it->controller.Resolve());
        if (!pc) { it = views.erase(it); continue; }
        if (!pc->IsLocalPlayerController()) { Reconcile(*it,pc,{}); it = views.erase(it); continue; }
        if (thisViewer)
        {
            using BrtUiJournal::Stage;
            BrtUiJournal::Note(actor,(slots.left || slots.right) ?
                Stage::HasShoulder : Stage::EmptyShoulders);
            if (close) BrtUiJournal::Note(actor,Stage::Closed);
            if (actor->CurrentLauncher && actor->CurrentLauncher == slots.left)
                BrtUiJournal::Note(actor,Stage::SelectedLeft);
            if (actor->CurrentLauncher && actor->CurrentLauncher == slots.right)
                BrtUiJournal::Note(actor,Stage::SelectedRight);
            Remember(*it,actor,slots.left,owns);
            if (slots.right != slots.left) Remember(*it,actor,slots.right,owns);
        }
        Update(*it,pc,actor,close,possess);
        ++it;
    }
}
}

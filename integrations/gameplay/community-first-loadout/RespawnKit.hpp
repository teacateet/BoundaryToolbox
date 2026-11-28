#pragma once
#include "RespawnWindow.hpp"
#include "RespawnItemId.hpp"
#include "Reconnect.hpp"
#ifdef GetCommandLine
#undef GetCommandLine
#endif
#include <chrono>
#include "../SDK/ProjectBoundary_parameters.hpp"
#include "../Client/DeathMenu.hpp"
#include <array>
#include <cstdio>

namespace brt::respawn_kit {
using namespace SDK;
using Names = std::array<FName, 4>;
struct Selection {
    Names ids{};
    std::array<bool, 4> present{};
};
inline bool Parse(const FPBInventoryNetworkConfig& config, Selection& out) {
    const int n = config.CharacterSlots.Num();
    if (n < 1 || n > 6 || n != config.InventoryItems.Num()) return false;
    int seen = 0;
    for (int i = 0; i < n; ++i) {
        const int slot = int(config.CharacterSlots[i]);
        if (slot <= 0 || slot > 6 || (seen & (1 << slot))) return false;
        seen |= 1 << slot;
        if (slot <= 4) { out.present[slot-1] = true; out.ids[slot-1] = config.InventoryItems[i]; }
    }
    return true;
}
inline APBCharacter* Living(APBPlayerController* pc, FName role) {
    auto* w = UWorld::GetWorld();
    auto* gm = w ? w->AuthorityGameMode : nullptr;
    if (!gm || !gm->IsA(APBGameMode::StaticClass()) || !pc || !pc->HasAuthority() ||
        !pc->NetConnection || pc->Player != pc->NetConnection || pc->NetConnection->PlayerController != pc ||
        !pc->PlayerState || !pc->PlayerState->IsA(APBPlayerState::StaticClass()) || pc->PBPlayerState != pc->PlayerState)
        return nullptr;
    auto* ps = pc->PBPlayerState;
    auto* pawn = pc->GetPBCharacter();
    if (!pawn || !InWindow(pawn->GetGameTimeSinceCreation(), ps->GetDeaths()) ||
        ps->bIsABot || ps->bIsInactive || ps->bOnlySpectator || role == FName{} ||
        ps->PossessedCharacterId != role || pawn != pc->K2_GetPawn() || pawn->GetController() != pc ||
        !pawn->HasAuthority() || !pawn->IsAlive() || pawn->IsInKilledStatus() ||
        !BrtDeathMenu::IsPveClass(UKismetSystemLibrary::GetPathName(gm->Class).ToString())) return nullptr;
    return pawn;
}
// 只接收游戏服务端在处理这名真人的预订时返回的原生确认，不使用客户端提供的物品类。
struct Order { APBPlayerController* pc{}; APBCharacter* pawn{}; FName role{}; Selection requested{}, accepted{}; bool ack{}; };
inline thread_local Order* current{};
inline void Receipt(UObject* object, const std::string& event, void* args) {
    if (!current || !args) return;
    const FPBInventoryNetworkConfig* cfg{}; FName role{};
    if (object == current->pc->PlayerState && event == "Function ProjectBoundary.PBPlayerState.ClientRefreshRolePreOrderingInventory") {
        auto& a=*static_cast<Params::PBPlayerState_ClientRefreshRolePreOrderingInventory*>(args);
        role=a.InRoleID; cfg=&a.InPreOrderingInventory;
    } else if (object == current->pc && event == "Function ProjectBoundary.PBPlayerController.ClientPreOrderUnlockInventory") {
        auto& a=*static_cast<Params::PBPlayerController_ClientPreOrderUnlockInventory*>(args);
        role=a.InRoleID; cfg=&a.InPreOrderingInventory;
    }
    if (cfg && role == current->role) { Selection confirmed; if (Parse(*cfg,confirmed)) { current->accepted=confirmed; current->ack=true; } }
}
inline std::uint8_t* Row(UDataTable* table, const char* type, FName id) {
    if (!table || !table->RowStruct || table->RowStruct->GetName()!=type) return nullptr;
    for (auto& entry:table->RowMap) if (entry.Key()==id) return entry.Value();
    return nullptr;
}
// 原生确认和角色装备范围使用干员专属 ID，实体蓝图使用重定向后的定义表 ID。
struct Item { FName selected{}, definition{}; UClass* cls{};
    bool Matches(FName id) const { return id != FName{} && (id == selected || id == definition); }
};
inline Item ResolveItem(FName role, FName id, bool gun) {
    auto* mgr=static_cast<UPBDataTableManager*>(USubsystemBlueprintLibrary::GetEngineSubsystem(UPBDataTableManager::StaticClass()));
    if (!mgr || id == FName{}) return {};
    auto* r=reinterpret_cast<FPBCharacterDefinitionRow*>(Row(mgr->CharacterDefinitionDataTable.Get(),"PBCharacterDefinitionRow",role));
    if (!r || r->bIsBot) return {};
    const auto& scope=gun ? r->CustomScope.WeaponScope : r->CustomScope.PodScope;
    bool approved=false;
    for(int i=0;i<scope.Num();++i) if(scope[i]==id){approved=true;break;}
    if (!approved) return {};
    FName definition=UPBDataTableFunctionLibrary::GetItemRedirectionID(id);
    auto* table=gun ? mgr->WeaponDefinitionDataTable.Get() : mgr->PodDefinitionDataTable.Get();
    const char* type=gun ? "PBWeaponDefinitionRow" : "PBPodDefinitionRow";
    auto* data=definition!=FName{} ? Row(table,type,definition) : nullptr;
    if (!data && (definition==FName{} || definition==id)) {
        definition=id; data=Row(table,type,id);
        if (!data) {
            auto key=ScopedDefinition(role.ToString(),id.ToString());
            if (key.empty()) return {};
            const std::wstring wide(key.begin(),key.end());
            definition=UKismetStringLibrary::Conv_StringToName(FString(wide.c_str()));
            data=Row(table,type,definition);
        }
    }
    if (!data) return {};
    UClass* cls{};
    if (gun) {
        auto* row=reinterpret_cast<FPBWeaponDefinitionRow*>(data);
        if (row) cls=UKismetSystemLibrary::LoadClassAsset_Blocking(row->WeaponClass);
    } else {
        auto* row=reinterpret_cast<FPBPodDefinitionRow*>(data);
        if (row) cls=UKismetSystemLibrary::LoadClassAsset_Blocking(row->PodClass);
    }
    return cls && cls->IsSubclassOf(gun ? APBWeapon::StaticClass() : APBLauncher::StaticClass()) ? Item{id,definition,cls} : Item{};
}
// 对空肩位记住移除前的弹药，反复卸下装回不能恢复弹药。
struct Balance { bool limited{}; int clip{}, total{}, magazine{}, stored{}; };
struct Budget { brt::reconnect::Identity pawn; std::chrono::steady_clock::time_point expires; std::array<Balance,4> slots{}; };
inline std::vector<Budget> budgets;
inline Balance& Slot(APBCharacter* pawn, int slot) {
    const brt::reconnect::Identity key(pawn);
    const auto now=std::chrono::steady_clock::now();
    for (auto it=budgets.begin(); it!=budgets.end();) {
        if (it->expires<=now) { it=budgets.erase(it); continue; }
        if (it->pawn==key) return it->slots[slot];
        ++it;
    }
    Budget b{key,now+std::chrono::seconds(6)};
    budgets.push_back(b); return budgets.back().slots[slot];
}
inline AActor* Spawn(UClass* cls, AActor* owner) {
    const auto transform=owner->GetTransform();
    auto* a=UGameplayStatics::BeginDeferredActorSpawnFromClass(owner,cls,transform,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,owner);
    return a ? UGameplayStatics::FinishSpawningActor(a,transform) : nullptr;
}
inline bool Gun(Order& order, int i, FName id) {
    auto* pawn=order.pawn; auto& inv=pawn->Inventory;
    if (i>=inv.Num() || inv.Num()>2 || pawn->PendingWeapon) return false;
    auto* old=inv[i]; if (!old || old->GetPawnOwner()!=pawn || id==FName{}) return false;
    auto item=ResolveItem(order.role,id,true);
    if (!item.cls || item.Matches(old->GetWeaponId()) || Living(order.pc,order.role)!=pawn) return false;
    auto* field=static_cast<UPBFieldModManager*>(USubsystemBlueprintLibrary::GetWorldSubsystem(pawn,UPBFieldModManager::StaticClass()));
    if (!field) return false;
    auto cfg=field->GetWeaponNetworkConfig(order.role,id);
    if (!item.Matches(cfg.WeaponID)) return false;
    auto* fresh=static_cast<APBWeapon*>(Spawn(item.cls,pawn)); if (!fresh) return false;
    fresh->InitWeapon(cfg,false);
    if (!item.Matches(fresh->GetWeaponId()) || Living(order.pc,order.role)!=pawn || i>=inv.Num() || inv[i]!=old) {fresh->K2_DestroyActor(); return false;}
    const bool selected=pawn->CurrentWeapon==old;
    const int total=old->GetCurrentAmmo(),clip=old->GetCurrentAmmoInClip(),n=inv.Num();
    // 保持原生槽位顺序；只释放被替换的旧实例。
    pawn->RemoveWeapon(old);
    pawn->AddWeapon(fresh);
    if (inv.Num()!=n || inv[n-1]!=fresh) {
                // 仅回滚这一次试装；若旧枪追加到尾部，放回原来的主副槽。
        pawn->RemoveWeapon(fresh);fresh->K2_DestroyActor();pawn->AddWeapon(old);
        if(inv.Num()==n && inv[n-1]==old && i!=n-1) std::swap(inv[i],inv[n-1]);
        if(selected)pawn->EquipPendingWeapon(old);
        return false;
    }
    if (i!=n-1) std::swap(inv[i],inv[n-1]);
    fresh->SetCurrentAmmo(CapAmmo(fresh->GetCurrentAmmo(),total));
    fresh->SetCurrentAmmoInClip(CapAmmo(fresh->GetCurrentAmmoInClip(),clip));
    if (selected) pawn->EquipPendingWeapon(fresh);
    old->K2_DestroyActor();fresh->ForceNetUpdate();pawn->ForceNetUpdate();
    return true;
}
inline bool Pod(Order& order,int i,FName id) {
    auto* pawn=order.pawn; auto* v=pawn->CurrentVehicle;
    if (!v || v->GetCharacter()!=pawn) return false;
    auto& old=i==2 ? v->LeftLauncher : v->RightLauncher;
    auto& carried=i==2 ? pawn->CurrentLeftLauncher : pawn->CurrentRightLauncher;
    // 原生待机挂点属于载具，角色的主动引用可以为空。非空时仍须指向同一挂载。
    if ((carried && carried!=old) || (old && (old->GetOwnerPawn()!=pawn || old->GetSavedData().ID==id)) || (!old && id==FName{})) return false;
    const bool active = carried!=nullptr;
    auto* before=old; APBLauncher* fresh{};
    if (id!=FName{}) {
        auto item=ResolveItem(order.role,id,false);
        if (!item.cls || (before && item.Matches(before->GetSavedData().ID)) || Living(order.pc,order.role)!=pawn) return false;
        fresh=static_cast<APBLauncher*>(Spawn(item.cls,v)); if (!fresh) return false;
        fresh->SavedData.ID=id; fresh->LauncherPosition=i==2 ? EPBLauncherPosition::Left : EPBLauncherPosition::Right;
    }
    if (Living(order.pc,order.role)!=pawn || before!=old) {if(fresh) fresh->K2_DestroyActor();return false;}
    auto& cap=Slot(pawn,i);
    if (before) { const auto& m=before->Magazine; cap={true,m.AmmoInClip,m.TotalAmmo,m.AmmoInMagazine,m.StoreAmmoInClip}; }
    if (fresh && cap.limited) {
        auto& m=fresh->Magazine; m.AmmoInClip=CapAmmo(m.AmmoInClip,cap.clip); m.TotalAmmo=CapAmmo(m.TotalAmmo,cap.total);
        m.AmmoInMagazine=CapAmmo(m.AmmoInMagazine,cap.magazine);m.StoreAmmoInClip=CapAmmo(m.StoreAmmoInClip,cap.stored);
    }
    if (pawn->CurrentLauncher==before && before) pawn->CurrentLauncher=fresh;
    old=fresh;if(active)carried=fresh;
    if(i==2){v->OnRep_LeftActiveSlotItem();if(active)pawn->OnRep_CurrentLeftLauncher();}else{v->OnRep_RightActiveSlotItem();if(active)pawn->OnRep_CurrentRightLauncher();}
    if (before) before->K2_DestroyActor();
    if (fresh) fresh->ForceNetUpdate();v->ForceNetUpdate();pawn->ForceNetUpdate();
    return true;
}
struct Scope {
    Order own{}; Order* prior{}; bool active{};
    Scope(UObject* o,const std::string& event,void* args) {
        Receipt(o,event,args);
        if (!args || !o || event!="Function ProjectBoundary.PBPlayerController.ServerPreOrderInventory" ||
            !o->IsA(APBPlayerController::StaticClass())) return;
        auto& a=*static_cast<Params::PBPlayerController_ServerPreOrderInventory*>(args);
        own.pc=static_cast<APBPlayerController*>(o); own.role=a.InRoleID; own.pawn=Living(own.pc,own.role);
        if (!own.pawn || !Parse(a.InPreOrderingInventory,own.requested)) return;
        prior=current;current=&own;active=true;
    }
    ~Scope() noexcept {
        if (!active) return; current=prior;
        try {
            if (!own.ack || Living(own.pc,own.role)!=own.pawn) return;
            for(int i=0;i<4;++i) if(own.requested.present[i] && own.accepted.present[i] && own.requested.ids[i]==own.accepted.ids[i] &&
                Living(own.pc,own.role)==own.pawn && (i<2 ? Gun(own,i,own.accepted.ids[i]) : Pod(own,i,own.accepted.ids[i])))
                std::printf("[BRT-RESPAWN-KIT] slot=%d age=%.3f applied=1\n",i+1,own.pawn->GetGameTimeSinceCreation());
        } catch (...) {}
    }
};
}

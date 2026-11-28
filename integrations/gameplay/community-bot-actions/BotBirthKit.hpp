#pragma once
#include "../SDK.hpp"
#include "../Logging/LogManager.h"
#include <algorithm>
#include <random>
#include <string>

namespace brt::bot_birth {
using namespace SDK;
inline FName Name(const std::string& s) {
    const std::wstring w(s.begin(),s.end());
    return UKismetStringLibrary::Conv_StringToName(FString(w.c_str()));
}
template<class T> inline T* Find(UDataTable* t,const char* type,FName id) {
    if(!t || id==FName{} || !t->RowStruct || t->RowStruct->GetName()!=type)return nullptr;
    for(auto& r:t->RowMap)if(r.Key()==id)return reinterpret_cast<T*>(r.Value());
    return nullptr;
}
// 只解析实际所属的干员或武器前缀。抽样之前仍须通过原表的槽位范围。
inline FName Base(FName id,const std::string& owner) {
    auto s=id.ToString(),prefix=owner+"_";
    if(s.starts_with(prefix) && s.size()>prefix.size())return Name(s.substr(prefix.size()));
    return id;
}
inline int Draw(int count) {
    static thread_local std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<int>(0,count-1)(rng);
}
inline bool DirectPod(const std::string& cls) {
    return cls=="PBEMPGrenadeLauncher_BP_C" || cls=="PBGrenadeLauncher_CQB_BP_C" ||
           cls=="BP_PBImpulseGrenadeLauncher_C";
}
inline const TArray<FName>* Parts(const FPBWeaponCustomScope& s,EPBPartSlotType slot) {
    // 仅改变已存在的消焰器和瞄具槽，不改变供弹、枪机和特殊射击方式。
    if(slot==EPBPartSlotType::Muzzle)return &s.MuzzleScope;
    if(slot==EPBPartSlotType::Sight_Optical)return &s.SightOpticalScope;
    return nullptr;
}
inline void Gun(UPBDataTableManager* m,const FPBCharacterDefinitionRow& role,const std::string& owner,APBCharacter* pawn) {
    auto* gun=pawn->CurrentWeapon;
    if(!gun || gun->GetPawnOwner()!=pawn || (pawn->PendingWeapon && pawn->PendingWeapon!=gun) || !gun->HasAuthority()){ServerLog("[BRT-BOT-KIT] skip parts: weapon transition");return;}
    const auto id=gun->GetWeaponId();
    auto def=Base(id,owner);bool allowed=false;
    for(auto v:role.CustomScope.WeaponScope)if(v==id || Base(v,owner)==def){allowed=true;break;}
    if(!allowed){ServerLog("[BRT-BOT-KIT] skip parts: role scope "+id.ToString());return;}
    auto* row=Find<FPBWeaponDefinitionRow>(m->WeaponDefinitionDataTable.Get(),"PBWeaponDefinitionRow",def);
    if(!row){ServerLog("[BRT-BOT-KIT] skip parts: weapon definition "+def.ToString());return;}
    // AI 实机复制的是 PartNetworkConfig，复制到本次调用的独立数组。
    auto cfg=gun->PartNetworkConfig;
    int n=cfg.WeaponPartConfigs.Num();
    if(!n || n>12 || n!=cfg.WeaponPartSlotTypeArray.Num() || Base(cfg.WeaponID,owner)!=def){ServerLog("[BRT-BOT-KIT] skip parts: incomplete "+id.ToString());return;}
    unsigned seen=0;bool changed=false;
    for(int i=0;i<n;++i) {
        auto slot=cfg.WeaponPartSlotTypeArray[i];unsigned bit=unsigned(slot);
        if(bit>12 || (seen&(1u<<bit)))return;seen|=1u<<bit;
        auto* pool=Parts(row->CustomScope,slot);
        if(!pool || pool->Num()<2 || pool->Num()>128)continue;
        auto choice=(*pool)[Draw(pool->Num())];
        const auto current=cfg.WeaponPartConfigs[i].WeaponPartID;
        auto base=Base(choice,def.ToString()),prior=Base(current,def.ToString());
        if(prior==base || !Find<FPBWeaponPartDefinitionRow>(m->WeaponPartDefinitionDataTable.Get(),"PBWeaponPartDefinitionRow",base))continue;
        // 保留原生配置使用的 ID 形式；不同配件不继承旧配件外观。
        if(current==FName{})continue;
        cfg.WeaponPartConfigs[i]={};cfg.WeaponPartConfigs[i].WeaponPartID=current==prior?base:choice;changed=true;
    }
    if(!changed){ServerLog("[BRT-BOT-KIT] unchanged parts "+id.ToString());return;}
    const int total=gun->GetCurrentAmmo(),clip=gun->GetCurrentAmmoInClip();
    // 已部署原枪只更新现有配置槽，使用原生复制通知，不重新初始化整把枪。
    for(int i=0;i<n;++i)gun->PartNetworkConfig.WeaponPartConfigs[i]=cfg.WeaponPartConfigs[i];
    gun->OnRep_PartNetworkConfig();
    gun->SetCurrentAmmo((std::min)(total,gun->GetCurrentAmmo()));
    gun->SetCurrentAmmoInClip((std::min)(clip,gun->GetCurrentAmmoInClip()));
    gun->ForceNetUpdate();
    const auto& actual=gun->PartNetworkConfig;
    if(actual.WeaponPartConfigs.Num()!=n || actual.WeaponPartSlotTypeArray.Num()!=n)return;
    for(int i=0;i<n;++i)if(actual.WeaponPartSlotTypeArray[i]!=cfg.WeaponPartSlotTypeArray[i] ||
        actual.WeaponPartConfigs[i].WeaponPartID!=cfg.WeaponPartConfigs[i].WeaponPartID)return;
    std::string slots;for(int i=0;i<n;++i)if(Parts(row->CustomScope,cfg.WeaponPartSlotTypeArray[i]))slots+=" "+actual.WeaponPartConfigs[i].WeaponPartID.ToString();
    ServerLog("[BRT-BOT-KIT] parts role="+owner+" weapon="+id.ToString()+slots);
}
inline void Pod(UPBDataTableManager* m,const FPBCharacterDefinitionRow& role,const std::string& owner,APBCharacter* pawn) {
    auto* v=pawn->CurrentVehicle;
    if(!v || v->GetCharacter()!=pawn || !v->HasAuthority() || pawn->CurrentLauncher)return;
    bool left=v->LeftLauncher && (!v->RightLauncher || Draw(2)==0);
    auto& mounted=left?v->LeftLauncher:v->RightLauncher;
    auto& carried=left?pawn->CurrentLeftLauncher:pawn->CurrentRightLauncher;
    auto* old=mounted;
    // 实机待机肩挂只出现在载具上，角色的主动挂载引用可以为空。
    const bool activeSlot=carried!=nullptr;
    if(!old || (activeSlot && carried!=old) || old->GetOwnerPawn()!=pawn || !old->IsStandby() || old->bIsFiring || old->bPendingFiring)return;
    auto& scope=role.CustomScope.PodScope;
    if(scope.Num()<2 || scope.Num()>64)return;
    auto id=scope[Draw(scope.Num())],base=Base(id,owner);
    if(Base(old->SavedData.ID,owner)==base)return;
    auto* row=Find<FPBPodDefinitionRow>(m->PodDefinitionDataTable.Get(),"PBPodDefinitionRow",base);
    if(!row)return;
    auto* cls=UKismetSystemLibrary::LoadClassAsset_Blocking(row->PodClass);
    if(!cls || !cls->IsSubclassOf(APBLauncher::StaticClass()) || !DirectPod(cls->GetName()))return;
    auto transform=old->GetTransform();
    auto* a=UGameplayStatics::BeginDeferredActorSpawnFromClass(v,cls,transform,ESpawnActorCollisionHandlingMethod::AlwaysSpawn,old->GetOwner());
    if(!a)return;
    auto* next=static_cast<APBLauncher*>(a);
    next->SavedData.ID=id;next->LauncherPosition=old->LauncherPosition;
    if(UGameplayStatics::FinishSpawningActor(a,transform)!=a || mounted!=old || carried!=(activeSlot?old:nullptr)){a->K2_DestroyActor();return;}
    auto& mag=next->Magazine;auto& cap=old->Magazine;
    mag.AmmoInClip=(std::min)(mag.AmmoInClip,cap.AmmoInClip);mag.TotalAmmo=(std::min)(mag.TotalAmmo,cap.TotalAmmo);
    mag.AmmoInMagazine=(std::min)(mag.AmmoInMagazine,cap.AmmoInMagazine);mag.StoreAmmoInClip=(std::min)(mag.StoreAmmoInClip,cap.StoreAmmoInClip);
    auto equip=[&](APBLauncher* item){mounted=item;if(activeSlot)carried=item;
        if(left){v->OnRep_LeftActiveSlotItem();if(activeSlot)pawn->OnRep_CurrentLeftLauncher();}
        else{v->OnRep_RightActiveSlotItem();if(activeSlot)pawn->OnRep_CurrentRightLauncher();}};
    equip(next);
    if(mounted!=next || (activeSlot && carried!=next) || next->GetOwnerPawn()!=pawn || next->SavedData.ID!=id){equip(old);next->K2_DestroyActor();return;}
    old->K2_DestroyActor();next->ForceNetUpdate();v->ForceNetUpdate();pawn->ForceNetUpdate();
    ServerLog("[BRT-BOT-KIT] pod role="+owner+" item="+id.ToString());
}
inline void Apply(APBAIController* ai,APBCharacter* pawn) {
    if(!ai || pawn->GetController()!=ai || !pawn->HasAuthority() || !pawn->IsAlive() ||
        !pawn->PBPlayerState || !pawn->PBPlayerState->bIsABot || pawn->CharacterID==FName{})return;
    auto* m=static_cast<UPBDataTableManager*>(USubsystemBlueprintLibrary::GetEngineSubsystem(UPBDataTableManager::StaticClass()));
    if(!m)return;
    auto owner=pawn->CharacterID.ToString();if(owner.starts_with("AI_"))owner=owner.substr(3);
    auto* role=Find<FPBCharacterDefinitionRow>(m->CharacterDefinitionDataTable.Get(),"PBCharacterDefinitionRow",Name("AI_"+owner));
    if(!role || !role->bIsBot)return;
    ServerLog("[BRT-BOT-KIT] birth role="+owner+" age="+std::to_string(pawn->GetGameTimeSinceCreation()));
    Gun(m,*role,owner,pawn);Pod(m,*role,owner,pawn);
}
}

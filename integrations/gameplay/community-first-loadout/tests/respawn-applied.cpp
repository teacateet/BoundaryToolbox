#include "RespawnKit.hpp"
#include <cassert>
#include <iostream>
using namespace SDK;
int main(){
    using namespace brt::respawn_kit;
    APBCharacter pawn;APBPlayerController pc;APBPlayerState ps;APBVehicle vehicle;
    Connection net{&pc};APBGameMode gm;pc.Class=pc.StaticClass();ps.Class=ps.StaticClass();
    UClass game{"OSS-normal",gm.StaticClass()};gm.Class=&game;UWorld::GetWorld()->AuthorityGameMode=&gm;
    pc.NetConnection=pc.Player=&net;pc.PlayerState=pc.PBPlayerState=&ps;pc.pawn=&pawn;pawn.pc=&pc;
    const FName role{"PROBE"},gun{"PROBE_GSW-DMR"},pod{"PROBE_TAC-EMP"};ps.PossessedCharacterId=role;
    APBWeapon old,second;old.owner=second.owner=&pawn;old.id={"GSW-AR"};old.total=11;old.clip=3;
    pawn.Inventory={&old,&second};pawn.CurrentWeapon=&old;pawn.CurrentVehicle=&vehicle;vehicle.character=&pawn;
    APBLauncher oldPod;oldPod.owner=&vehicle;oldPod.SavedData.ID={"ATK-HE"};oldPod.Magazine={1,2,1,0};
    pawn.CurrentLeftLauncher=vehicle.LeftLauncher=&oldPod;
    RowType charType{"PBCharacterDefinitionRow"},gunType{"PBWeaponDefinitionRow"},podType{"PBPodDefinitionRow"};
    FPBCharacterDefinitionRow limits;limits.CustomScope.WeaponScope={gun};limits.CustomScope.PodScope={pod};
    FPBWeaponDefinitionRow weapon{APBWeapon::StaticClass()};FPBPodDefinitionRow mount{APBLauncher::StaticClass()};
    UDataTable roles{&charType,{{role,reinterpret_cast<std::uint8_t*>(&limits)}}};
    UDataTable weapons{&gunType,{{{"GSW-DMR"},reinterpret_cast<std::uint8_t*>(&weapon)}}};
    UDataTable pods{&podType,{{{"TAC-EMP"},reinterpret_cast<std::uint8_t*>(&mount)}}};
    USubsystemBlueprintLibrary::data={{&roles},{&weapons},{&pods}};
    USubsystemBlueprintLibrary::field.config={{"GSW-DMR"}};
    Params::OrderMsg order{role,{{1,3},{gun,pod}}};pawn.age=0.4f;
    auto submit=[&](bool confirmed){Scope s(&pc,"Function ProjectBoundary.PBPlayerController.ServerPreOrderInventory",&order);
      if(confirmed)Receipt(&ps,"Function ProjectBoundary.PBPlayerState.ClientRefreshRolePreOrderingInventory",&order);};
    submit(false);assert(pawn.CurrentWeapon==&old && pawn.CurrentLeftLauncher==&oldPod);
    submit(true);auto* changed=pawn.CurrentWeapon;auto* changedPod=pawn.CurrentLeftLauncher;
    assert(pc.pawn==&pawn && ps.deaths==1 && changed!=&old && changed->id.value=="GSW-DMR");
    assert(pawn.Inventory.Num()==2 && pawn.Inventory[0]==changed && pawn.Inventory[1]==&second);
    assert(changedPod!=&oldPod && changedPod==vehicle.LeftLauncher && changedPod->SavedData.ID==pod);
    assert(changed->clip==3 && changed->total==11 && changedPod->Magazine.TotalAmmo==2);
    changed->clip=1;changedPod->Magazine.TotalAmmo=0;submit(true);
    assert(UGameplayStatics::guns.size()==1 && UGameplayStatics::pods.size()==1 && changed->clip==1 && changedPod->Magazine.TotalAmmo==0);
    order.InPreOrderingInventory={{3},{{}}};submit(true);assert(!pawn.CurrentLeftLauncher);
    order.InPreOrderingInventory={{3},{pod}};submit(true);assert(vehicle.LeftLauncher && !pawn.CurrentLeftLauncher && vehicle.LeftLauncher->Magazine.TotalAmmo==0);
    auto* standby=vehicle.LeftLauncher;standby->SavedData.ID={"ATK-HE"};submit(true);
    assert(vehicle.LeftLauncher!=standby && vehicle.LeftLauncher->SavedData.ID==pod && !pawn.CurrentLeftLauncher && vehicle.LeftLauncher->Magazine.TotalAmmo==0);
    pawn.age=5.0f;auto* atDeadline=vehicle.LeftLauncher;order.InPreOrderingInventory={{3},{{}}};submit(true);
    assert(pc.pawn==&pawn && vehicle.LeftLauncher==atDeadline && !pawn.CurrentLeftLauncher && ps.deaths==1);
    assert(!ResolveItem(role,{"PEACE_GSW-DMR"},true).cls);
    UPBDataTableFunctionLibrary::redirect={"BAD-ROW"};assert(!ResolveItem(role,gun,true).cls);
    for(auto* w:UGameplayStatics::guns)delete w;for(auto* p:UGameplayStatics::pods)delete p;
    std::cout<<"PASS server handler fixture: same pawn, weapon and pod applied; no receipt, duplicate, empty pod and deadline checked\n";
}

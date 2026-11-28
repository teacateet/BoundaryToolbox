#pragma once
// 只模拟此功能消费的 SDK 对象；替换槽位、确认回包、窗口和弹量逻辑全部调用生产头文件。
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace SDK {
struct FName {std::string value;std::string ToString() const{return value;}bool operator==(const FName&)const=default;};
using FString=std::wstring;
struct UClass {std::string name;UClass* base{};bool IsSubclassOf(UClass* other){return this==other||base==other;}};
#define BRT_TYPE(T) static UClass* StaticClass(){static UClass c{#T};return &c;}
struct UObject {UClass* Class{};bool IsA(UClass* c){return Class&&Class->IsSubclassOf(c);}};
struct AActor:UObject {AActor* owner{};bool destroyed{};int updates{};void K2_DestroyActor(){destroyed=true;}
  void ForceNetUpdate(){++updates;}int GetTransform(){return 1;}bool HasAuthority(){return true;}};
template<class T>struct Array:std::vector<T>{using std::vector<T>::vector;int Num()const{return int(this->size());}};
struct FPBInventoryNetworkConfig {Array<int> CharacterSlots;Array<FName> InventoryItems;};
struct FPBWeaponNetworkConfig {FName WeaponID;};
struct APBCharacter;struct APBPlayerController;struct APBVehicle;
struct APBWeapon:AActor {BRT_TYPE(Weapon) FName id;int total{60},clip{30};
  AActor* GetPawnOwner(){return owner;}FName GetWeaponId(){return id;}
  int GetCurrentAmmo(){return total;}int GetCurrentAmmoInClip(){return clip;}
  void SetCurrentAmmo(int n){total=n;}void SetCurrentAmmoInClip(int n){clip=n;}
  void InitWeapon(FPBWeaponNetworkConfig cfg,bool){id=cfg.WeaponID;}};
enum class EPBLauncherPosition {Left,Right};
struct APBLauncher:AActor {BRT_TYPE(Launcher) struct Saved {FName ID;} SavedData;
  struct Mag {int AmmoInClip=8,TotalAmmo=20,AmmoInMagazine=8,StoreAmmoInClip=8;} Magazine;
  EPBLauncherPosition LauncherPosition{};APBCharacter* GetOwnerPawn();
  Saved GetSavedData(){return SavedData;}};
struct APBVehicle:AActor {APBCharacter* character{};APBLauncher *LeftLauncher{},*RightLauncher{};
  auto GetCharacter(){return character;}void OnRep_LeftActiveSlotItem(){}void OnRep_RightActiveSlotItem(){}};
inline APBCharacter* APBLauncher::GetOwnerPawn(){return static_cast<APBVehicle*>(owner)->character;}
struct APBCharacter:AActor {APBPlayerController* pc{};Array<APBWeapon*> Inventory;
  APBWeapon *PendingWeapon{},*CurrentWeapon{};APBVehicle* CurrentVehicle{};
  APBLauncher *CurrentLeftLauncher{},*CurrentRightLauncher{},*CurrentLauncher{};float age{};bool alive{true};
  float GetGameTimeSinceCreation(){return age;}auto GetController(){return pc;}bool IsAlive(){return alive;}
  bool IsInKilledStatus(){return !alive;}void RemoveWeapon(APBWeapon* w){std::erase(Inventory,w);}
  void AddWeapon(APBWeapon* w){Inventory.push_back(w);}void EquipPendingWeapon(APBWeapon* w){CurrentWeapon=w;}
  void OnRep_CurrentLeftLauncher(){}void OnRep_CurrentRightLauncher(){}};
struct APBPlayerState:AActor {BRT_TYPE(State) FName PossessedCharacterId;int deaths{1};
  bool bIsABot{},bIsInactive{},bOnlySpectator{};int GetDeaths(){return deaths;}};
struct Connection {APBPlayerController* PlayerController{};};
struct APBPlayerController:AActor {BRT_TYPE(Controller) APBCharacter* pawn{};
  Connection *NetConnection{},*Player{};APBPlayerState *PlayerState{},*PBPlayerState{};
  auto GetPBCharacter(){return pawn;}auto K2_GetPawn(){return pawn;}};
struct APBGameMode:AActor {BRT_TYPE(Mode)};
struct UWorld {APBGameMode* AuthorityGameMode{};static UWorld* GetWorld(){static UWorld w;return &w;}};
struct Text {std::string s;auto ToString(){return s;}};
struct UKismetSystemLibrary {static Text GetPathName(UClass* c){return {c->name};}
  static UClass* LoadClassAsset_Blocking(UClass* c){return c;}};
struct UKismetStringLibrary {static FName Conv_StringToName(const FString& s){return {std::string(s.begin(),s.end())};}};
struct RowType {std::string name;auto GetName(){return name;}};
struct Entry {FName id;std::uint8_t* data;auto Key(){return id;}auto Value(){return data;}};
struct UDataTable {RowType* RowStruct;std::vector<Entry> RowMap;};
struct Table {UDataTable* t{};auto Get(){return t;}};
struct FPBWeaponDefinitionRow {UClass* WeaponClass;};struct FPBPodDefinitionRow {UClass* PodClass;};
struct FPBCharacterDefinitionRow {bool bIsBot{};struct Scope{Array<FName> WeaponScope,PodScope;} CustomScope;};
struct UPBDataTableManager {BRT_TYPE(Tables) Table CharacterDefinitionDataTable,WeaponDefinitionDataTable,PodDefinitionDataTable;};
struct UPBFieldModManager {BRT_TYPE(Field) FPBWeaponNetworkConfig config;
  FPBWeaponNetworkConfig GetWeaponNetworkConfig(FName,FName){return config;}};
struct USubsystemBlueprintLibrary {static inline UPBDataTableManager data;static inline UPBFieldModManager field;
  static auto GetEngineSubsystem(UClass*){return &data;}static auto GetWorldSubsystem(APBCharacter*,UClass*){return &field;}};
struct UPBDataTableFunctionLibrary {static inline FName redirect;static FName GetItemRedirectionID(FName){return redirect;}};
enum class ESpawnActorCollisionHandlingMethod {AlwaysSpawn};
struct UGameplayStatics {static inline std::vector<APBWeapon*> guns;static inline std::vector<APBLauncher*> pods;
  static AActor* BeginDeferredActorSpawnFromClass(AActor*,UClass* c,int,ESpawnActorCollisionHandlingMethod,AActor* owner){
    AActor* a;if(c->IsSubclassOf(APBWeapon::StaticClass())){auto* w=new APBWeapon;guns.push_back(w);a=w;}
    else{auto* p=new APBLauncher;pods.push_back(p);a=p;}a->owner=owner;a->Class=c;return a;}
  static AActor* FinishSpawningActor(AActor* a,int){return a;}};
namespace Params {struct OrderMsg{FName InRoleID;FPBInventoryNetworkConfig InPreOrderingInventory;};
using PBPlayerState_ClientRefreshRolePreOrderingInventory=OrderMsg;using PBPlayerController_ClientPreOrderUnlockInventory=OrderMsg;
using PBPlayerController_ServerPreOrderInventory=OrderMsg;}
#undef BRT_TYPE
}
namespace brt::reconnect {struct Identity {SDK::APBCharacter* p;explicit Identity(SDK::APBCharacter* v):p(v){}
  bool operator==(const Identity&)const=default;};}
namespace BrtDeathMenu {inline bool IsPveClass(const std::string& s){return s=="OSS-normal";}}

#include "BoundaryInventory.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include <cstdint>
#include <type_traits>

using namespace brt::spawn;
using namespace SDK;
using WeaponInit = void (APBWeapon::*)(const FPBWeaponNetworkConfig&, bool);
static_assert(std::is_same_v<decltype(&APBWeapon::InitWeapon), WeaponInit>);
static_assert(static_cast<int>(EPBPartSlotType::Muzzle) == 1 && static_cast<int>(EPBPartSlotType::Sight_Optical) == 6 &&
              static_cast<int>(EPBPartSlotType::AmmoStorageDevice) == 9 && static_cast<int>(EPBPartSlotType::Stock) == 10);
static_assert(static_cast<int>(EPBCharacterSlotType::FirstWeapon) == 1 && static_cast<int>(EPBCharacterSlotType::SecondWeapon) == 2 &&
              static_cast<int>(EPBCharacterSlotType::LeftPod) == 3 && static_cast<int>(EPBCharacterSlotType::RightPod) == 4 &&
              static_cast<int>(EPBCharacterSlotType::MeleeWeapon) == 5 && static_cast<int>(EPBCharacterSlotType::Mobility) == 6);
static_assert(static_cast<int>(EPBSkinClass::None) == 0 && static_cast<int>(EPBSkinClass::EPBSkinClass_MAX) == 6 &&
              static_cast<int>(EPBPartSlotType::ReceiverMain) == 12);

// 用编译器的 /Zs 在真实声明上解析模板，不链接社区镜像的运行时代码。
bool CheckRoleDeclarations(const Loadout& loadout, const FPBRoleNetworkConfig& role, FName (*names)(const Name&),
                           bool (*commit)(const FPBRoleNetworkConfig&, const Loadout&)) {
    return BuildInventory(loadout, role, names, commit);
}
void CheckWeaponDeclarations(APBWeapon& weapon, const FPBWeaponNetworkConfig& config) {
    InitSpawnedWeapon(weapon, config);
}
struct DeclaredHostBatch {
    APBWeapon* PrimaryWeapon();
    APBWeapon* SecondaryWeapon();
    bool StageAppearance(APBWeapon&, const Weapon&);
    void Publish();
};
using HostBeginBatch = std::unique_ptr<DeclaredHostBatch>(*)(const FPBRoleNetworkConfig&, std::uint64_t);
Result CheckInventoryCall(Consumer& member, std::uint64_t generation, std::string_view body, const FPBRoleNetworkConfig& base,
                          FName (*names)(const Name&), HostBeginBatch create) {
    return AcceptAtInventorySpawn(member,generation,body,base,names,create);
}
Result CheckDressedInventoryCall(Consumer& member, std::uint64_t generation, std::string_view body, const FPBRoleNetworkConfig& base,
    const FPBRoleNetworkConfig& stock, const UPBCharacterDefaultConfig& defaults, FName (*names)(const Name&), HostBeginBatch create) {
    return AcceptAtInventorySpawn(member,generation,body,base,stock,defaults,names,create);
}

#include "OwnerRoleMenu.hpp"
bool CheckOwnerFieldMod(APBPlayerState& state, const Member& owner, std::string_view body,
    const FPBFieldModRoleGameSavedNetworkConfig& original, const TArray<FName>& roles,
    const TArray<int32>& quotas, FName (*names)(const Name&)) {
    return InitOwnerFieldMod(state, owner, body, original, roles, quotas, names);
}

#include "OwnedBirth.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace brt::spawn;
namespace {
void Check(bool value, const char* condition) {
    if (!value) throw std::runtime_error(condition);
}
template<class T> struct Array : std::vector<T> {
    void Add(const T& value) { this->push_back(value); }
    int Num() const { return static_cast<int>(this->size()); }
};
struct NativePart {
    Name WeaponPartID, WeaponPartSkinID, WeaponPartSpecialSkinID, WeaponPartSkinPaintingID;
};
struct NativeConfig {
    Array<unsigned> WeaponPartSlotTypeArray;
    Array<NativePart> WeaponPartConfigs;
    Name WeaponID{None}, WeaponClassID{None}, OrnamentID{None};
};
struct Role {
    Name CharacterID;
    NativeConfig FirstWeaponPartData, SecondWeaponPartData;
    struct { Array<unsigned> SkinClassArray; Array<Name> SkinIDArray; Name SkinPaintingID; } CharacterData;
    struct { Name ID,SkinID; } LeftLauncherData,RightLauncherData,MeleeWeaponData;
    struct { Name MobilityModuleID; } MobilityModuleData;
    struct { Array<unsigned> CharacterSlots; Array<Name> InventoryItems; } InventoryData;
};
struct Pawn {};
struct Controller {
    const Pawn* possessed{};
    const Pawn* K2_GetPawn() const { return possessed; }
};
struct Actor {
    const Pawn* owner;
    Name item, weaponClass;
    NativeConfig saved{}, observed{};
    int calls{};
    const Pawn* GetPawnOwner() const { return owner; }
    Name GetWeaponId() const { return item; }
    Name GetWeaponClassID() const { return weaponClass; }
    NativeConfig GetPBWeaponPartSaveConfig() const { return saved; }
    void InitWeapon(const NativeConfig& value, bool modelOnly) {
        Check(!modelOnly, "model-only initialization");
        ++calls;
        observed = value;
    }
};
const auto names = [](const Name& value) { return value; };
Json Read(const std::filesystem::path& path) {
    std::ifstream in(path);
    Check(static_cast<bool>(in), "missing fixture");
    return Json::parse(in);
}
Selection Selected(const Json& envelope) {
    const auto& value = envelope.at("loadout");
    return {value.at("CharacterID"), value.at("FirstWeaponPartData").at("WeaponID"),
            value.at("SecondWeaponPartData").at("WeaponID")};
}
void CheckParts(const Actor& actor, const Json& wire, bool nativeReceiver) {
    const auto& value = actor.observed;
    Check(actor.calls == 1 && value.WeaponID == actor.item && value.WeaponClassID == actor.weaponClass &&
          value.OrnamentID == wire.at("OrnamentID") && value.WeaponPartConfigs.Num() == 10 + int(nativeReceiver || wire.contains("ReceiverMainAppearance")) &&
          value.WeaponPartSlotTypeArray.Num() == value.WeaponPartConfigs.Num(), "gun identity or slot count");
    for (int index = 0; index != 10; ++index) {
        const auto& expected = wire.at("WeaponPartConfigs").at(index);
        const auto& part = value.WeaponPartConfigs[index];
        Check(value.WeaponPartSlotTypeArray[index] == wire.at("WeaponPartSlotTypeArray").at(index) &&
              part.WeaponPartID == expected.at("WeaponPartID") && part.WeaponPartSkinID == expected.at("WeaponPartSkinID") &&
              part.WeaponPartSpecialSkinID == expected.at("WeaponPartSpecialSkinID") &&
              part.WeaponPartSkinPaintingID == expected.at("WeaponPartSkinPaintingID"), "lost part or appearance");
    }
    if (wire.contains("ReceiverMainAppearance")) {
        const auto& look = wire.at("ReceiverMainAppearance");
        const auto& part = value.WeaponPartConfigs[10];
        Check(value.WeaponPartSlotTypeArray[10] == 12 &&
              part.WeaponPartID == look.at("WeaponPartID") && part.WeaponPartSkinID == look.at("WeaponPartSkinID") &&
              part.WeaponPartSkinPaintingID == look.at("WeaponPartSkinPaintingID") &&
              part.WeaponPartSpecialSkinID == (nativeReceiver ? "host.special" : None), "wrong receiver paint or base");
    } else if (nativeReceiver) {
        const auto& part = value.WeaponPartConfigs[10];
        Check(value.WeaponPartSlotTypeArray[10] == 12 && part.WeaponPartID == "5001002" &&
              part.WeaponPartSkinID == "host.skin" && part.WeaponPartSpecialSkinID == "host.special" &&
              part.WeaponPartSkinPaintingID == "host.paint", "lost original receiver material");
    }
}
}

int main(int argc, char** argv) try {
    Check(argc == 2, "expected fixture directory");
    const auto base = std::filesystem::path(argv[1]);
    auto firstLife = Read(base / "primaryLife.json");
    auto respawn = Read(base / "respawn.json");
    auto selected = Selected(firstLife);
    auto secondLifeSelection = Selected(respawn);
    Member member{firstLife.at("roomId"), firstLife.at("connectionId"), firstLife.at("authenticatedSteamId")};
    Pawn pawn, other;
    Controller controller{&pawn};
    OwnedBirth<Controller, Pawn, Actor> birth(member, controller);
    Role role{};
    role.CharacterID = selected.role;
    role.FirstWeaponPartData.WeaponID = "GSW-DMR";  // 测试宿主使用武器定义表 ID。
    role.FirstWeaponPartData.WeaponClassID = "class.dmr";
    role.SecondWeaponPartData.WeaponID = "PROBE_GSW-FOP";  // 此槽使用角色限定键。
    role.SecondWeaponPartData.WeaponClassID = "class.fop";
    Actor dmr{&pawn, "GSW-DMR", "class.dmr"}, fop{&pawn, "PROBE_GSW-FOP", "class.fop"};
    dmr.saved.WeaponID = dmr.item;
    dmr.saved.WeaponClassID = dmr.weaponClass;
    dmr.saved.WeaponPartSlotTypeArray.Add(12);
    dmr.saved.WeaponPartConfigs.Add({"5001002","host.skin","host.special","host.paint"});

    auto wrongRole = role;
    wrongRole.FirstWeaponPartData.WeaponID = "PEACE_GSW-DMR";
    Check(!birth.Begin(1, 94, selected, pawn, wrongRole, names), "other role's weapon accepted");
    Check(birth.Begin(1, 94, selected, pawn, role, names) && birth.Prepare(94, firstLife.dump()), "first birth");
    struct { Name SpacesuitID, ArmBadgeID, HeadAccessoryID; } factoryNames{"PROBE_ORIGINAL","ABGProbeDefault","HONONE"};
    int fullPlans=0;
    auto acceptRole = [&](const Role& prepared) {
        ++fullPlans;
        return prepared.FirstWeaponPartData.WeaponID==role.FirstWeaponPartData.WeaponID &&
            prepared.SecondWeaponPartData.WeaponID==role.SecondWeaponPartData.WeaponID &&
            prepared.LeftLauncherData.ID=="PROBE_MISSILE-SPARROW" && prepared.RightLauncherData.ID=="PROBE_INFO-PROBE" &&
            prepared.MobilityModuleData.MobilityModuleID=="PROBE_FCM-GRAPPLE" && prepared.InventoryData.InventoryItems.size()==6;
    };
    Check(!birth.WithInventory(1,94,other,role,role,factoryNames,names,acceptRole) &&
        !birth.WithInventory(1,94,pawn,wrongRole,role,factoryNames,names,acceptRole) &&
        birth.WithInventory(1,94,pawn,role,role,factoryNames,names,acceptRole) && fullPlans==1,
        "foreign owner or lost role equipment before weapon initialization");
    Actor impostor{&pawn, dmr.item, dmr.weaponClass}, foreign{&other, fop.item, fop.weaponClass};
    Check(birth.InitCurrent(1, 94, pawn, &dmr, &fop, impostor, names) == BirthInit::Rejected &&
          birth.InitCurrent(1, 94, pawn, &dmr, &foreign, foreign, names) == BirthInit::Rejected &&
          birth.InitCurrent(1, 94, pawn, &fop, &dmr, dmr, names) == BirthInit::Rejected &&
          birth.InitCurrent(1, 94, pawn, &dmr, &dmr, dmr, names) == BirthInit::Rejected, "wrong slot or entity accepted");
    auto savedClass = fop.weaponClass;
    fop.weaponClass = "other.class";
    Check(birth.InitCurrent(1, 94, pawn, &dmr, &fop, fop, names) == BirthInit::Rejected, "wrong live class");
    fop.weaponClass = savedClass;
    fop.saved.WeaponID = dmr.item;
    Check(birth.InitCurrent(1, 94, pawn, &dmr, &fop, fop, names) == BirthInit::Rejected, "wrong saved config");
    fop.saved.WeaponID = None;
    if (firstLife.at("loadout").at("FirstWeaponPartData").contains("ReceiverMainAppearance")) {
        dmr.saved.WeaponPartConfigs[0].WeaponPartID = "different.receiver";
        Check(birth.InitCurrent(1, 94, pawn, &dmr, &fop, dmr, names) == BirthInit::Rejected,
              "attached paint to unrelated receiver");
        dmr.saved.WeaponPartConfigs[0].WeaponPartID = "5001002";
    }
    Check(!dmr.calls && !fop.calls && !impostor.calls && !foreign.calls, "mismatch changed an actor");
    std::cout << "PASS entity, owner, role, slot, native ID and class mismatches initialized zero weapons\n";

    Check(birth.InitCurrent(1, 94, pawn, &dmr, nullptr, dmr, names) == BirthInit::Initialized &&
          birth.InitCurrent(1, 94, pawn, &dmr, &fop, fop, names) == BirthInit::Initialized, "sequential dual birth");
    Check(birth.Prepare(94, firstLife.dump()) &&
          birth.InitCurrent(1, 94, pawn, &dmr, &fop, dmr, names) == BirthInit::AlreadyInitialized &&
          birth.InitCurrent(1, 94, pawn, &impostor, &fop, impostor, names) == BirthInit::Rejected, "duplicate or replacement");
    CheckParts(dmr, firstLife.at("loadout").at("FirstWeaponPartData"), true);
    CheckParts(fop, firstLife.at("loadout").at("SecondWeaponPartData"), false);
    std::cout << "PASS one pawn, sequential DMR/FOP, one call per slot, all 20 saved parts and separate receiver validated\n";

    auto roleAfter = role;
    roleAfter.SecondWeaponPartData = {};
    Actor nextDmr{&pawn, dmr.item, dmr.weaponClass};
    Check(birth.Begin(2, 95, secondLifeSelection, pawn, roleAfter, names) && !birth.Prepare(95, firstLife.dump()) &&
          birth.Prepare(95, respawn.dump()), "stale envelope or new life");
    Check(birth.InitCurrent(1, 95, pawn, &nextDmr, nullptr, nextDmr, names) == BirthInit::Rejected &&
          birth.InitCurrent(2, 94, pawn, &nextDmr, nullptr, nextDmr, names) == BirthInit::Rejected &&
          birth.InitCurrent(2, 95, pawn, &nextDmr, &fop, fop, names) == BirthInit::Rejected, "old life or unloaded slot");
    controller.possessed = &other;
    Check(birth.InitCurrent(2, 95, pawn, &nextDmr, nullptr, nextDmr, names) == BirthInit::Rejected, "possession changed");
    controller.possessed = &pawn;
    Check(!nextDmr.calls && birth.InitCurrent(2, 95, pawn, &nextDmr, nullptr, nextDmr, names) == BirthInit::Initialized &&
          nextDmr.calls == 1 && fop.calls == 1, "new life delivery");
    std::cout << "PASS reused pawn, old life and possession rejected, current life initializes only its selected weapon\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

#include "BoundaryInventory.hpp"
#include "OwnerRoleMenu.hpp"
#include "RoomHttp.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

using namespace brt::spawn;
template<class T> struct Array : std::vector<T> {
    using std::vector<T>::vector;
    int Num() const { return static_cast<int>(this->size()); }
    void Add(const T& value) { this->push_back(value); }
};
enum class Slot : unsigned char {};
struct PartConfig { Name WeaponPartID, WeaponPartSkinID, WeaponPartSpecialSkinID, WeaponPartSkinPaintingID; };
struct GunConfig {
    Array<Slot> WeaponPartSlotTypeArray;
    Array<PartConfig> WeaponPartConfigs;
    Name OrnamentID, WeaponID, WeaponClassID;
};
struct Attachment { Name ID, SkinID; };
struct Mobility { Name MobilityModuleID; };
struct CharacterConfig {
    Array<Slot> SkinClassArray;
    Array<Name> SkinIDArray;
    Name SkinPaintingID;
    bool operator==(const CharacterConfig& other) const {
        return SkinClassArray==other.SkinClassArray && SkinIDArray==other.SkinIDArray && SkinPaintingID==other.SkinPaintingID;
    }
};
struct Defaults { Name SpacesuitID, ArmBadgeID, HeadAccessoryID; };
struct Inventory { Array<Slot> CharacterSlots; Array<Name> InventoryItems; };
struct Role {
    Name CharacterID;
    CharacterConfig CharacterData;
    GunConfig FirstWeaponPartData, SecondWeaponPartData;
    Attachment LeftLauncherData, RightLauncherData, MeleeWeaponData;
    Mobility MobilityModuleData;
    Inventory InventoryData;
};
struct Actor {
    GunConfig config;
    Appearance body;
    std::array<Part, 10> appearance;
    int initCount = 0;
    void InitWeapon(const GunConfig& value, bool modelOnly) {
        if (modelOnly) throw std::runtime_error("spawned display model");
        config = value; ++initCount;
    }
};

void Check(bool ok, const char* problem) {
    if (!ok) throw std::runtime_error(problem);
}
struct Live {
    Role role;
    Actor first, second;
    unsigned publications = 0;
    bool appearanceSupported = true;
    std::function<void()> onAppearance;
};
struct Batch {
    Live& live;
    Role role;
    Actor first, second;
    Actor* PrimaryWeapon() { return role.FirstWeaponPartData.WeaponID == None ? nullptr : &first; }
    Actor* SecondaryWeapon() { return role.SecondWeaponPartData.WeaponID == None ? nullptr : &second; }
    bool StageAppearance(Actor& actor, const Weapon& raw) {
        if (!live.appearanceSupported) return false;
        if (live.onAppearance) live.onAppearance();
        actor.body = raw.appearance; actor.appearance = raw.parts;
        return true;
    }
    void Publish() {
        live.role = role; live.first = first; live.second = second;
        ++live.publications;
    }
};

std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing service envelope");
    return {std::istreambuf_iterator<char>(input), {}};
}
Member User(const Json& doc) { return {doc.at("roomId"),doc.at("connectionId"),doc.at("authenticatedSteamId")}; }
Selection Chosen(const Json& doc) {
    const auto& data = doc.at("loadout");
    return {data.at("CharacterID"),data.at("FirstWeaponPartData").at("WeaponID"),data.at("SecondWeaponPartData").at("WeaponID")};
}
Role Approved(const Selection& selection) {
    Role base;
    base.CharacterID = selection.role;
    base.CharacterData = {{Slot(4),Slot(5),Slot(1),Slot(3)},
        {"host.badge","host.extra","host.suit","host.head"},"host.paint"};
    base.FirstWeaponPartData.WeaponID = selection.primary;
    base.FirstWeaponPartData.WeaponClassID = "approved-primary-base";
    base.SecondWeaponPartData.WeaponID = selection.secondary;
    base.SecondWeaponPartData.WeaponClassID = "approved-secondary-base";
    base.RightLauncherData = {"previous-probe", "previous-pod-skin"};
    base.InventoryData.InventoryItems.Add("previous-pawn");
    return base;
}
Result Apply(Consumer& source, std::uint64_t pawn, const std::string& bytes, Role base, Live& live) {
    return AcceptAtInventorySpawn(source,pawn,bytes,base,[](const Name& n){return n;},
        [&](const Role& role, std::uint64_t) {return std::make_unique<Batch>(Batch{live,role,{},{}});});
}
void CompareEveryPart(const Actor& actor, const Json& expected) {
    const size_t count = expected.contains("ReceiverMainAppearance") ? 11 : 10;
    Check(actor.config.WeaponPartConfigs.size() == count && actor.config.WeaponPartSlotTypeArray.size() == count,"part count");
    for (size_t i=0;i<10;++i) {
        const auto& want = expected.at("WeaponPartConfigs").at(i);
        const auto& got = actor.config.WeaponPartConfigs[i];
        Check(static_cast<int>(actor.config.WeaponPartSlotTypeArray[i]) == int(i+1) &&
            got.WeaponPartID == want.at("WeaponPartID") && got.WeaponPartSkinID == want.at("WeaponPartSkinID") &&
            got.WeaponPartSpecialSkinID == want.at("WeaponPartSpecialSkinID") &&
            got.WeaponPartSkinPaintingID == want.at("WeaponPartSkinPaintingID") &&
            actor.appearance[i].archiveOrnament == want.at("ArchiveOrnament"), "part, skin or ornament lost");
    }
    if (expected.contains("ReceiverMainAppearance")) {
        const auto& row = expected.at("ReceiverMainAppearance");
        const auto& native = actor.config.WeaponPartConfigs[10];
        Check(static_cast<int>(actor.config.WeaponPartSlotTypeArray[10]) == 12 && native.WeaponPartID == row.at("WeaponPartID") &&
              native.WeaponPartSkinID == row.at("WeaponPartSkinID") && native.WeaponPartSkinPaintingID == row.at("WeaponPartSkinPaintingID"),
              "suite did not reach the native main receiver");
    }
    const auto& look = expected.at("ArchiveAppearance");
    Check(actor.body.skin == look.at("SkinItem") && actor.body.paint == look.at("PaintItem") &&
        actor.body.special == look.at("SpecialSlot") && actor.body.ornament == look.at("Ornament") &&
        actor.config.OrnamentID == expected.at("OrnamentID"),"body appearance lost");
}

struct MenuState {
    Array<Name> RoleArray;
    Array<Inventory> RoleInventoryNetworkConfigArray;
};
struct MenuPlayer {
    MenuState saved;
    Array<int> quotas;
    int initCount{};
    void ClientInitFieldMod(const MenuState& roles, const Array<Name>&, const Array<int>& owned) {
        saved=roles; quotas=owned; ++initCount;
    }
};

void CheckInitialMenu(const Member& member, const std::string& response) {
    MenuState stock;
    Inventory stockSlot; stockSlot.CharacterSlots.Add(static_cast<Slot>(1)); stockSlot.InventoryItems.Add("SNIPER-DEFAULT");
    stock.RoleArray.Add("SNIPER"); stock.RoleInventoryNetworkConfigArray.Add(stockSlot);
    Array<Name> allowed; allowed.Add("PROBE"); allowed.Add("SNIPER");
    Array<int> quotas; quotas.Add(2); quotas.Add(0);
    MenuPlayer owner;
    const auto identity = [](const Name& value){return value;};
    Member stranger = member; stranger.connection = "previous-connection";
    Check(!InitOwnerFieldMod(owner,stranger,response,stock,allowed,quotas,identity) && owner.initCount == 0, "foreign menu");
    Check(InitOwnerFieldMod(owner,member,response,stock,allowed,quotas,identity),"initial field mod");
    Check(owner.quotas==quotas && owner.initCount==1 && owner.saved.RoleArray.size()==2, "room quotas or allowed roles changed");
    const auto it=std::find(owner.saved.RoleArray.begin(),owner.saved.RoleArray.end(),"PROBE");
    Check(it!=owner.saved.RoleArray.end(),"role missing");
    const Inventory& value=owner.saved.RoleInventoryNetworkConfigArray[it-owner.saved.RoleArray.begin()];
    Check(value.InventoryItems==std::vector<Name>({"PROBE_GSW-DMR","PROBE_GSW-FOP","PROBE_MISSILE-SPARROW",
        "PROBE_INFO-PROBE","MELEE-KNIFE","PROBE_FCM-GRAPPLE"}),"role menu did not restore recorded six slots");
    for(int n=0;n<6;++n) Check(static_cast<int>(value.CharacterSlots[n])==n+1,"wrong character slot enum");
}

void CheckLiveRoomHttp(bool local = false) {
    const auto options = Json::parse(std::cin);
    const auto port = options.at("port").get<unsigned short>();
    RoomHttp room = local ? RoomHttp::FromLocalServer(port).value() : RoomHttp(port,options.at("room"),options.at("credential"));
    const Member member{room.RoomId(),options.at("connection"),options.at("steam")};
    const Selection selected{"PROBE","PROBE_GSW-DMR","PROBE_GSW-FOP"};
    room.Admit(member);
    CheckInitialMenu(member,room.InitialMenu(member.connection));
    Consumer owner(member);
    Live live;
    Check(owner.Begin(1,1,selected),"HTTP begin");
    const auto reply = room.Prepare(member.connection,1,selected);
    Check(Apply(owner,1,reply,Approved(selected),live)==Result::Applied &&
        live.first.body.paint=="GSW-DMR_Original_PTMetalGold" && live.second.body.paint=="GSW-FOP_Original_PTElectroRed" &&
        live.role.LeftLauncherData.ID=="PROBE_MISSILE-SPARROW" && live.role.RightLauncherData.ID=="PROBE_INFO-PROBE","HTTP first equipment");
    Check(Apply(owner,1,room.Prepare(member.connection,1,selected),Approved(selected),live)==Result::AlreadyApplied,"HTTP retry");
    const Selection respawn{"PROBE","PROBE_GSW-DMR","None"};
    Check(owner.Begin(2,2,respawn) && Apply(owner,2,room.Prepare(member.connection,2,respawn),Approved(respawn),live)==Result::Applied &&
        live.second.initCount==0 && live.role.InventoryData.InventoryItems[1]==None,"HTTP respawn");
    bool closedMenu=false;
    try { room.InitialMenu(member.connection); } catch (const std::runtime_error&) { closedMenu=true; }
    Check(closedMenu,"late role menu would overwrite active selection");
    owner.Leave(); room.Leave(member.connection);
    std::cout<<"PASS C++ WinHTTP admit, initial six-slot owner menu and quotas, prepare, typed test-actor initialization, retry, respawn, leave\n";
}

void CheckCharacterHttp(const std::filesystem::path& dir) {
    for (const auto* file : {"characterLife.json", "clearHead.json"}) {
        const auto response = Read(dir/file);
        const auto wire = Json::parse(response);
        const auto& look = wire.at("loadout").at("CharacterData");
        const auto chosen = Chosen(wire);
        const Role base = Approved(chosen);
        // 顺序故意与宿主当前外观不同。用原作默认资产的 ID 匹配，不能照数组下标盖写。
        Role stock = base;
        const Defaults defaults{"PROBE_ORIGINAL","ABGProbeDefault","HONONE"};
        stock.CharacterData = {{Slot(3),Slot(4),Slot(1)}, {defaults.HeadAccessoryID,defaults.ArmBadgeID,defaults.SpacesuitID}, "factory.paint"};
        Consumer owner(User(wire));
        const auto life = wire.at("life").get<unsigned>();
        for (unsigned n=1; n<=life; ++n) Check(owner.Begin(n,700+n,chosen),"begin character life");
        Live live;
        auto publish = [&](const Role& staged, std::uint64_t) {return std::make_unique<Batch>(Batch{live,staged,{},{}});};
        const auto intern = [](const Name& n){return n;};
        const auto generation = 700+life;
        Check(AcceptAtInventorySpawn(owner,generation,response,base,intern,publish) == Result::HostDeclined &&
            live.publications==0,"legacy host silently ignored character appearance");
        auto wrong = stock;
        wrong.CharacterData.SkinIDArray[0]="unrelated";
        Check(AcceptAtInventorySpawn(owner,generation,response,base,wrong,defaults,intern,publish) == Result::HostDeclined &&
            live.publications==0,"unknown character category changed the pawn");
        Check(AcceptAtInventorySpawn(owner,generation,response,base,stock,defaults,intern,publish)==Result::Applied,
            "character appearance failed to publish");
        const auto& result=live.role.CharacterData;
        Check(result.SkinClassArray==base.CharacterData.SkinClassArray && result.SkinIDArray[0]==look.at("ArmBadgeID") &&
            result.SkinIDArray[1]=="host.extra" && result.SkinIDArray[2]==look.at("SpacesuitID") &&
            result.SkinIDArray[3]==look.at("HeadAccessoryID") && result.SkinPaintingID==look.at("SpacesuitSkinPaintingID"),
            "role appearance did not preserve all four fields and host-owned category");
        Check(base.CharacterData.SkinIDArray[2]=="host.suit" && stock.CharacterData.SkinIDArray[0]==defaults.HeadAccessoryID &&
            AcceptAtInventorySpawn(owner,generation,response,base,stock,defaults,intern,publish)==Result::AlreadyApplied &&
            live.publications==1,"defaults or peer shared appearance mutated");
    }
    std::cout<<"PASS real role HTTP: suit, paint, badge and head with explicit clear, host-only category, refused unmapped classes, one publish\n";
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("provide fixture directory");
    if (std::string_view(argv[1])=="--room-http") { CheckLiveRoomHttp(); return 0; }
    if (std::string_view(argv[1])=="--local-room-http") { CheckLiveRoomHttp(true); return 0; }
    const std::filesystem::path dir(argv[1]);
    if (std::filesystem::exists(dir/"characterLife.json")) { CheckCharacterHttp(dir); return 0; }
    const auto first = Read(dir/"primaryLife.json"), peer = Read(dir/"peerLife.json");
    const auto retry = Read(dir/"retry.json"), next = Read(dir/"respawn.json");
    const auto a = Json::parse(first), b = Json::parse(peer), c = Json::parse(next);
    const auto selected=Chosen(a), respawn=Chosen(c);
    const Role base=Approved(selected);
    Consumer owner(User(a)), other(User(b)); Live live, peerLive;
    Check(owner.Begin(1,101,selected) && other.Begin(1,501,Chosen(b)),"begin");
    Check(!owner.Begin(1,101,{"PROBE","PEACE_GSW-DMR","None"}),"cross-role weapon");
    Check(Apply(owner,101,peer,base,live) == Result::Rejected && Apply(owner,999,first,base,live) == Result::Rejected,"wrong account/pawn");
    for (auto key : {"roomId","connectionId","authenticatedSteamId"}) {
        auto wrong=a; wrong[key]="unrelated";
        Check(Apply(owner,101,wrong.dump(),base,live) == Result::Rejected,"wrong identity binding");
    }
    auto wrong=a; wrong["loadout"]["CharacterID"]="PEACE";
    Check(Apply(owner,101,wrong.dump(),base,live) == Result::Rejected,"wrong character");
    wrong=a; wrong["loadout"]["FirstWeaponPartData"]["WeaponPartSlotTypeArray"][1]=1;
    Check(Apply(owner,101,wrong.dump(),base,live) == Result::Rejected,"duplicate weapon slot");
    wrong=a; wrong["loadout"]["FirstWeaponPartData"]["WeaponClassID"]="different-class";
    Check(Apply(owner,101,wrong.dump(),base,live) == Result::HostDeclined,"unapproved weapon class");
    live.appearanceSupported=false;
    Check(Apply(owner,101,first,base,live)==Result::HostDeclined && live.publications==0,"partial appearance commit");
    live.appearanceSupported=true;
    Check(Apply(owner,101,first,base,live)==Result::Applied,"owner first life");
    CompareEveryPart(live.first,a["loadout"]["FirstWeaponPartData"]);
    CompareEveryPart(live.second,a["loadout"]["SecondWeaponPartData"]);
    Check(live.first.initCount==1 && live.second.initCount==1 && live.first.body.paint=="GSW-DMR_Original_PTMetalGold" &&
        live.second.body.paint=="GSW-FOP_Original_PTElectroRed" &&
        live.first.config.WeaponClassID=="approved-primary-base" && live.role.CharacterData==base.CharacterData, "approved class, base or appearance");
    Check(live.role.LeftLauncherData.ID=="PROBE_MISSILE-SPARROW" && live.role.RightLauncherData.ID=="PROBE_INFO-PROBE" &&
        live.role.MobilityModuleData.MobilityModuleID=="PROBE_FCM-GRAPPLE" && live.role.InventoryData.InventoryItems.size()==6 &&
        live.role.InventoryData.InventoryItems[2]==live.role.LeftLauncherData.ID && live.role.InventoryData.InventoryItems[3]==live.role.RightLauncherData.ID,
        "role slots or launchers");
    Check(Apply(owner,101,retry,base,live)==Result::AlreadyApplied && live.publications==1,"duplicate first life");
    wrong=a; wrong["loadout"]["LeftLauncherData"]["ID"]="None";
    Check(Apply(owner,101,wrong.dump(),base,live)==Result::Rejected,"changed retry");
    Check(Apply(other,501,peer,Approved(Chosen(b)),peerLive)==Result::Applied && peerLive.first.body.paint!=live.first.body.paint &&
        peerLive.role.LeftLauncherData.ID!=live.role.LeftLauncherData.ID,"other player isolation");
    Check(owner.Begin(2,102,respawn) && !owner.Begin(1,101,selected),"advance life");
    Check(Apply(owner,102,first,base,live)==Result::Rejected,"old first life");
    Check(Apply(owner,102,next,Approved(respawn),live)==Result::Applied && live.publications==2 && live.second.initCount==0 &&
        live.role.SecondWeaponPartData.WeaponID==None && live.role.SecondWeaponPartData.WeaponPartConfigs.empty() &&
        live.role.RightLauncherData.ID==None && live.role.RightLauncherData.SkinID==None &&
        live.role.InventoryData.InventoryItems[1]==None && live.role.InventoryData.InventoryItems[3]==None,"respawn clear");
    owner.Leave();
    Check(Apply(owner,102,next,Approved(respawn),live)==Result::Rejected,"connection left");
    Consumer departed(User(a)); Live waiting;
    Check(departed.Begin(1,801,selected),"departing player");
    waiting.onAppearance=[&]{departed.Leave();};
    Check(Apply(departed,801,first,base,waiting)==Result::HostDeclined && waiting.publications==0,"left during stage");
    std::cout<<"PASS two authenticated fixture members, DMR/FOP 20 slots plus appearances, six inventory slots, retries, respawn and rejected stale replies\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL "<<error.what()<<'\n'; return 1;
}

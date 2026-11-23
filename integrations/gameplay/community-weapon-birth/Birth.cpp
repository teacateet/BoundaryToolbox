#include "Birth.hpp"
#include "BirthWindow.hpp"
#include "BoundaryInventory.hpp"
#include "RolePaths.hpp"
#include "RoomHttp.hpp"
#include "SDK/ProjectBoundary_classes.hpp"
#include <cstdio>
#include <memory>
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

namespace {
using namespace SDK;
using namespace brt::spawn;
using brt::birth::Counts;

struct Watched {
    TSoftObjectPtr<UObject> owner;
    APBPlayerController* identity;    // 只作身份比较；每帧向引擎解析弱引用后再调用。
    RoomHttp room;
    Member member;
    std::vector<SavedMenuRole> rows;
    std::unique_ptr<Consumer> archive;
    brt::birth::Window window;
    std::uint64_t life{};
    std::uint64_t requests{};
    bool inTick{};
    APBCharacter* previousPawn{};
    Counts previousCounts{};
    bool sawPawn{};
    float previousTime{-1.0f};
};
std::shared_ptr<Watched> watched;

void Skipped(std::uint64_t life, const char* reason) {
    std::printf("[BRT-INVENTORY] newborn-skipped life=%llu reason=%s\n",
        static_cast<unsigned long long>(life), reason);
    std::fflush(stdout);
}

FName Intern(const Name& value) {
    if (value.empty() || value.size() > 256 ||
        value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != Name::npos)
        throw std::invalid_argument("invalid item ID");
    const std::wstring wide(value.begin(), value.end());
    return UKismetStringLibrary::Conv_StringToName(FString(wide.c_str()));
}

Counts CurrentCounts(APBPlayerState& state) {
    return {state.GetDeaths(), state.GetNumBulletsFired(), state.GetNumRocketsFired()};
}

bool UnusedPawn(Watched& session, APBPlayerController& pc, APBPlayerState& state, APBCharacter& pawn) {
    return pc.HasAuthority() && pawn.HasAuthority() && session.window.Owns(&pawn, pawn.GetGameTimeSinceCreation()) &&
           pc.K2_GetPawn() == &pawn && pawn.GetController() == &pc && pawn.IsAlive() &&
           session.window.Unused(CurrentCounts(state), pawn.GetGameTimeSinceCreation(),
               !UGameplayStatics::IsGamePaused(&pawn), !pc.IsLocalController(), pc.AcknowledgedPawn == &pawn,
               pawn.IsFiring(), pawn.IsADS() || pawn.IsTargeting());
}

bool UnusedWeapon(APBWeapon* weapon, APBCharacter& pawn) {
    if (!weapon || !weapon->HasAuthority()) return false;
    const auto state = weapon->GetCurrentState();
    return brt::birth::FreshWeapon(weapon->GetGameTimeSinceCreation(), weapon->GetPawnOwner() == &pawn,
        weapon->IsDeployed(), weapon->IsInReloadState(), weapon->IsStartADS(),
        state == EPBWeaponState::Idle, state == EPBWeaponState::Equipping, pawn.GetGameTimeSinceCreation());
}

bool Matches(const Name& selected, const Name& role, const Name& actual) {
    const Name prefix = role + "_";
    return selected != None && selected.starts_with(prefix) &&
           (selected == actual || std::string_view(selected).substr(prefix.size()) == actual);
}

struct Guns {
    const SavedMenuRole* row{};
    std::array<APBWeapon*, 2> actors{};
};

std::optional<Guns> Match(const Watched& session, APBCharacter& pawn) {
    auto* pawnClass = UGameplayStatics::GetObjectClass(&pawn);
    if (!pawnClass) return {};
    const auto classPath = UKismetSystemLibrary::GetPathName(pawnClass).ToString();
    const auto mode = UPBGameplayStatics::GetModeRuleSetting(&pawn);
    if (!mode.CharacterDefinitionIDScope.Num()) return {};
    const auto weapons = pawn.GetWeaponArray();
    if (weapons.Num() < 1 || weapons.Num() > 2) return {};
    std::array<Name, 2> actual{};
    for (int i = 0; i < weapons.Num(); ++i) {
        if (!weapons[i] || !weapons[i]->HasAuthority() || weapons[i]->GetPawnOwner() != &pawn ||
            !brt::birth::BornWithPawn(weapons[i]->GetGameTimeSinceCreation(), pawn.GetGameTimeSinceCreation()) ||
            (i && weapons[0] == weapons[i])) return {};
        actual[i] = weapons[i]->GetWeaponId().ToString();
    }
    const SavedMenuRole* approved = nullptr;
    for (int i = 0; i < mode.CharacterDefinitionIDScope.Num(); ++i) {
        const auto role = mode.CharacterDefinitionIDScope[i].ToString();
        const auto def = std::find_if(brt::birth::rolePaths.begin(), brt::birth::rolePaths.end(),
            [&](const auto& r) { return r.id == role; });
        if (def == brt::birth::rolePaths.end()) return {};
        if (def->path != classPath) continue;
        // 同模型限定干员也可能持有同一支枪。只有引擎给出的角色前缀才可消除歧义。
        const bool excluded = std::any_of(actual.begin(), actual.end(), [&](const auto& id) {
            std::string_view prefix;
            for (const auto& other : brt::birth::rolePaths)
                if (other.id.size() > prefix.size() && id.starts_with(std::string(other.id) + "_")) prefix = other.id;
            return !prefix.empty() && prefix != role;
        });
        if (excluded) continue;
        const auto row = std::find_if(session.rows.begin(), session.rows.end(), [&](const auto& r) { return r.id == role; });
        if (approved || row == session.rows.end()) return {};
        approved = &*row;
    }
    if (!approved) return {};
    Guns found{approved, {}};
    for (int i = 0; i < weapons.Num(); ++i) {
        const bool first = Matches(approved->items[0], approved->id, actual[i]);
        const bool second = Matches(approved->items[1], approved->id, actual[i]);
        if (first == second) return {};
        auto& slot = found.actors[first ? 0 : 1];
        if (slot) return {};
        slot = weapons[i];
    }
    for (unsigned i = 0; i < 2; ++i)
        if ((found.actors[i] == nullptr) != (approved->items[i] == None)) return {};
    return found;
}

bool Build(const Weapon& saved, APBWeapon& actor, FPBWeaponNetworkConfig& out) {
    const auto original = actor.GetPBWeaponPartSaveConfig();
    const auto id = actor.GetWeaponId(), cls = actor.GetWeaponClassID(), none = Intern(None);
    if (id == none || cls == none || (original.WeaponID != none && original.WeaponID != id) ||
        (original.WeaponClassID != none && original.WeaponClassID != cls)) return false;
    out.WeaponID = Intern(saved.id);
    out.WeaponClassID = cls;
    if (!FillWeapon(saved, out, Intern, &original)) return false;
    out.WeaponID = id;
    return true;
}

bool SameParts(const FPBWeaponNetworkConfig& actual, const FPBWeaponNetworkConfig& desired) {
    const auto count = actual.WeaponPartConfigs.Num();
    if (actual.WeaponID != desired.WeaponID || actual.WeaponClassID != desired.WeaponClassID ||
        actual.OrnamentID != desired.OrnamentID || count != actual.WeaponPartSlotTypeArray.Num() ||
        count != desired.WeaponPartConfigs.Num() || count != desired.WeaponPartSlotTypeArray.Num()) return false;
    std::array<bool, 13> seen{};
    for (int i = 0; i < count; ++i) {
        const auto slot = static_cast<unsigned>(actual.WeaponPartSlotTypeArray[i]);
        if (!slot || slot >= seen.size() || seen[slot]) return false;
        seen[slot] = true;
        int j = 0;
        while (j < count && desired.WeaponPartSlotTypeArray[j] != actual.WeaponPartSlotTypeArray[i]) ++j;
        if (j == count) return false;
        const auto& a = actual.WeaponPartConfigs[i];
        const auto& b = desired.WeaponPartConfigs[j];
        if (a.WeaponPartID != b.WeaponPartID || a.WeaponPartSkinID != b.WeaponPartSkinID ||
            a.WeaponPartSkinPaintingID != b.WeaponPartSkinPaintingID || a.WeaponPartSpecialSkinID != b.WeaponPartSpecialSkinID) return false;
    }
    return true;
}

enum class BirthStep { Waiting, Finished };
BirthStep ApplyAtBirth(Watched& session, APBPlayerController& pc, APBPlayerState& state, APBCharacter& pawn, bool first) {
    if (first) {
        const auto inventory = pawn.GetWeaponArray();
        std::printf("[BRT-INVENTORY] observed-birth life=%llu pawn-age=%.4f inventory=%d holding=%d acknowledged=%d\n",
            static_cast<unsigned long long>(session.life), static_cast<double>(pawn.GetGameTimeSinceCreation()),
            inventory.Num(), pawn.GetCurrentWeapon() != nullptr, pc.AcknowledgedPawn == &pawn);
        std::fflush(stdout);
    }
    if (!UnusedPawn(session, pc, state, pawn)) {
        const char* reason = !session.window.Owns(&pawn, pawn.GetGameTimeSinceCreation()) ? "newborn-window-expired-or-replaced" :
            pc.AcknowledgedPawn == &pawn ? "possession-acknowledged" : "pawn-not-in-unused-state";
        Skipped(session.life,reason);
        return BirthStep::Finished;
    }
    if (!state.HasSelectedRole()) return BirthStep::Waiting;
    const auto found = Match(session, pawn);
    if (!found) return BirthStep::Waiting;
    const Selection chosen{found->row->id, found->row->items[0], found->row->items[1]};
    const auto request = session.requests + 1;
    if (!session.archive->Begin(request, session.life, chosen)) { Skipped(session.life,"request-out-of-order"); return BirthStep::Finished; }
    session.requests = request;
    const auto reply = session.room.OwnerBirthPlan(session.member, request, chosen);
    const auto applied = session.archive->Accept(session.life, reply, [&](const Loadout& loadout) {
        std::array<FPBWeaponNetworkConfig, 2> configs{};
        const Weapon* plans[]{&loadout.primary, &loadout.secondary};
        for (unsigned i = 0; i < 2; ++i)
            if (found->actors[i] && !Build(*plans[i], *found->actors[i], configs[i])) return false;
        return brt::birth::VerifyOccupied(found->actors, [&](unsigned i, APBWeapon* weapon) {
            if (watched.get() != &session || !state.HasSelectedRole() || !UnusedPawn(session, pc, state, pawn)) return false;
            const auto still = Match(session, pawn);
            if (!still || still->row != found->row || still->actors != found->actors) return false;
            const bool already = SameParts(weapon->GetPBWeaponPartSaveConfig(), configs[i]);
            if (!already) {
                // 主枪已进入手中时保留它，同时继续处理尚未使用的副枪。
                if (pawn.GetCurrentWeapon() == weapon || !UnusedWeapon(weapon, pawn)) {
                    std::printf("[BRT-INVENTORY] newborn-slot-skipped life=%llu slot=%u reason=weapon-in-use\n",
                        static_cast<unsigned long long>(session.life), i + 1);
                    std::fflush(stdout);
                    return false;
                }
                weapon->InitWeapon(configs[i], false);
            }
            const bool readback = SameParts(weapon->GetPBWeaponPartSaveConfig(), configs[i]);
            std::printf("[BRT-INVENTORY] newborn-weapon life=%llu slot=%u submitted=%d parts-readback=%d\n",
                static_cast<unsigned long long>(session.life), i + 1, !already, readback);
            std::fflush(stdout);
            return readback;
        });
    });
    if (applied != Result::Applied) Skipped(session.life,"archive-or-weapon-validation");
    return BirthStep::Finished;
}
}

bool BrtWatchOwnerBirth(SDK::APBPlayerController* pc, const brt::spawn::RoomHttp& room,
                        const std::string& steam, const std::vector<brt::spawn::SavedMenuRole>& rows) noexcept {
    try {
        if (!pc || !pc->HasAuthority() || !pc->PlayerState || !pc->PlayerState->IsA(APBPlayerState::StaticClass())) return false;
        auto* state = static_cast<APBPlayerState*>(pc->PlayerState);
        if (state->GetDefaultIDStr().ToString() != steam) return false;
        GUID guid{};
        wchar_t formatted[40]{};
        if (FAILED(CoCreateGuid(&guid)) || StringFromGUID2(guid, formatted, 40) != 39) return false;
        Name connection;
        for (unsigned i = 1; i < 37; ++i) connection.push_back(static_cast<char>(formatted[i]));
        Member member{room.RoomId(), connection, steam};
        auto next = std::make_shared<Watched>(Watched{UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(pc),
            pc, room, member, rows, std::make_unique<Consumer>(member), {}, 0, 0, false});
        if (!pc->GetPBCharacter()) next->window.Wait(CurrentCounts(*state), UGameplayStatics::GetTimeSeconds(pc));
        watched = std::move(next);
        return true;
    } catch (const std::exception&) { return false; }
}

void BrtTickOwnerBirth() noexcept {
    auto session = watched;
    if (!session || session->inTick) return;
    session->inTick = true;
    struct Reset { bool& bit; ~Reset() { bit = false; } } reset{session->inTick};
    try {
        auto* object = UKismetSystemLibrary::Conv_SoftObjectReferenceToObject(session->owner);
        if (object != session->identity || !object || !object->IsA(APBPlayerController::StaticClass())) {
            watched.reset();
            return;
        }
        auto& pc = *static_cast<APBPlayerController*>(object);
        if (!pc.HasAuthority() || !pc.PlayerState || !pc.PlayerState->IsA(APBPlayerState::StaticClass())) return;
        auto& state = *static_cast<APBPlayerState*>(pc.PlayerState);
        auto* pawn = pc.GetPBCharacter();
        const auto counts = CurrentCounts(state);
        const float now = UGameplayStatics::GetTimeSeconds(&pc);
        if (!pawn || pc.K2_GetPawn() != pawn || pawn->IsInKilledStatus()) {
            session->window.Wait(counts, now);
            session->sawPawn = false;
            return;
        }
        // 有些重生在相邻两帧直接替换角色，没有一帧可见的空 Pawn。
        if (session->sawPawn && session->previousPawn != pawn && session->previousCounts.deaths < counts.deaths)
            session->window.Wait(session->previousCounts, session->previousTime);
        session->previousPawn = pawn;
        session->previousTime = now;
        session->previousCounts = counts;
        session->sawPawn = true;
        const bool first = session->window.Start(pawn, pawn->GetGameTimeSinceCreation(), now);
        if (!first && !session->window.Pending()) return;
        if (first) ++session->life;
        if (state.GetDefaultIDStr().ToString() != session->member.steam) { watched.reset(); return; }
        try {
            if (ApplyAtBirth(*session, pc, state, *pawn, first) == BirthStep::Finished) session->window.Finish();
        } catch (const std::exception&) { session->window.Finish(); Skipped(session->life,"local-archive-unavailable"); }
    } catch (const std::exception&) {}
}

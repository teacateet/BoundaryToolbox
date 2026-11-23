#include "GrappleInput.hpp"
#include <iostream>

using namespace SDK;
int checks;
void Check(bool pass, const char* message) {
    ++checks;
    if (!pass) throw std::runtime_error(message);
}
int Count(std::string_view event) {
    return int(std::count_if(records.begin(), records.end(), [&](const auto& row){ return row.find(event) != row.npos; }));
}
bool Row(std::initializer_list<std::string_view> parts) {
    return std::any_of(records.begin(), records.end(), [&](const auto& row) {
        return std::all_of(parts.begin(), parts.end(), [&](auto part){ return row.find(part) != row.npos; });
    });
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    savedPath = argv[1];
    UPBOptions mapping; options = &mapping; mapping.actual = {{47},{93},{47},{0}};
    APBCharacter actor, remote;
    APlayerController pc, other; APBPlayerState ps; APBCatchingModule gear, swap;
    APBMobilityModule different;
    pc.PlayerState = &ps; actor.control = &pc; pc.pawn = &actor; actor.current = &gear;
    gear.OwnerCharacter = &actor; swap.OwnerCharacter = &actor;
    remote.control = &other; other.local = false; other.pawn = &remote;
    auto event = [&](const char* name){ BrtGrappleInput::OnCharacter(&actor, name); };
    auto step = [&](int p, int r, int d){ ++frame; pc.pressed = p; pc.released = r; pc.down = d; };
    auto tick = [&](){ event("Function Engine.Actor.ReceiveTick"); };
    inScope = false; step(47,0,47); tick(); inScope = true;
    BrtGrappleInput::OnCharacter(&remote, "K2_LaunchHook");
    event("WrongK2_PrepareToLaunchHook");
    Check(records.empty() && !pc.inputReads && !other.inputReads, "exclude other modes, remote viewers and unrelated callbacks");
    step(0,0,0); tick();
    Check(Row({"life=1 ", "event=scope", "mapped=2", "slot=1 module=grapple", "cooled=1 allowed=1"}), "first scope uses deduplicated actual keys and owned slot");
    step(47,0,47); tick(); event("Function ProjectBoundary.PBCharacter.K2_PrepareToLaunchHook");
    gear.CurrentHookStatus = 1; actor.usingHook = true; event("K2_LaunchHook"); tick();
    Check(Count("event=mapped-key") == 1 && Row({"press=1 event=mapped-key edge=1 down=1"}), "one mapped press across repeated same-frame callbacks");
    Check(Row({"press=1 event=prepare", "phase=0"}) && Row({"press=1 event=launch", "phase=1"}), "first real prepare and launch kept distinct");
    Check(mapping.queries == 2, "one mapping read per observed frame");
    step(0,0,47); tick(); Check(Count("event=mapped-key") == 1, "holding over another frame does not create a press");
    step(0,47,0); tick(); event("K2_ResetHook");
    Check(Row({"press=1 event=mapped-key edge=2 down=0"}), "first release");
    gear.CurrentHookStatus = 0; gear.bIsInCoolDown = false; actor.usingHook = false; actor.allowed = false;
    step(93,0,93); tick(); step(0,93,0); tick();
    Check(Row({"press=2 event=mapped-key edge=1", "phase=0 cool=0 enabled=1", "cooled=1 allowed=0 using=0"}), "second press distinguishes cooled slot from character refusal");
    Check(Row({"press=2 event=mapped-key edge=2"}) && !Row({"press=2 event=prepare"}) && Count("event=launch") == 1, "second release without a native prepare or launch");
    actor.allowed = true; mapping.actual = {{26}};
    step(93,0,93); tick(); Check(Count("event=mapped-key") == 4 && Row({"event=mapping", "mapped=1"}), "old key excluded immediately after remap");
    step(26,0,26); event("K2_PrepareToLaunchHook"); tick(); event("K2_CancelPreparationOfLaunchHook");
    Check(Row({"press=3 event=prepare", "allowed=1"}) && Row({"press=3 event=cancel"}) && !Row({"press=3 event=launch"}), "native cancellation separate from second press failure and from a launch");
    step(0,26,0); tick();
    step(0,0,0); tick(); pc.pressed = pc.released = 26; event("K2_PrepareToLaunchHook"); tick();
    Check(Row({"press=4 event=mapped-key edge=3 down=0"}) && Count("edge=3 ") == 1, "both frame edges observed when they become visible after tick");
    mapping.actual = {{158}}; step(158,0,158); tick();
    Check(Row({"press=5 event=mapped-key edge=1", "mapped=1"}), "same-count physical remap takes effect");
    options = nullptr; step(158,0,158); tick(); Check(Row({"event=mapping", "mapped=-1"}), "options unavailable remains distinct from zero mapped keys");
    options = &mapping; mapping.actual.clear(); step(158,0,158); tick(); Check(Row({"event=mapping", "mapped=0"}), "explicit unbound action");
    mapping.actual = {{158}};
    const auto before = records.size(); pc.pawn = &remote; step(158,0,158); tick();
    Check(records.size()==before, "controller must currently possess this character"); pc.pawn = &actor;
    actor.current = nullptr; step(158,0,158); tick();
    Check(Row({"press=6 event=mapped-key", "slot=0 module=none phase=-1"}), "records a press even when slot was lost after first launch");
    actor.current = &different; step(0,158,0); tick();
    Check(Row({"press=6 event=mapped-key edge=2", "module=other phase=-1"}), "replacement non-grapple module remains distinct");
    actor.current = &swap; swap.OwnerCharacter = &remote; step(158,0,158); tick();
    Check(Row({"press=7 event=mapped-key", "module=foreign phase=-1"}), "never read another character's grapple state");
    swap.OwnerCharacter = &actor; swap.valid = false; step(0,158,0); tick();
    Check(Row({"press=7 event=mapped-key edge=2", "slot=0 module=none"}), "expired slot is treated as absent");
    swap.valid = true; swap.generation++; step(158,0,158); tick();
    Check(Row({"press=8 event=mapped-key", "slot=4 module=grapple"}), "a recreated slot gets a new room-local number");
    event("NotifyBeKilled"); const auto atDeath = records.size(); step(0,158,0); tick();
    BrtGrappleInput::OnCharacter(&remote, "ReceivePossessed");
    Check(records.size()==atDeath, "delayed alive flag after death cannot continue the old life");
    ps.deaths++; step(158,0,158); tick();
    Check(Row({"life=2 ", "press=1 event=mapped-key"}), "same pawn revived between sampled ticks starts a fresh life");
    actor.alive = false; step(0,158,0); tick(); actor.alive = true; step(158,0,158); tick();
    Check(Row({"life=3 ", "press=1 event=mapped-key"}), "observed dead and alive transition also separates lives");
    const bool nativeInactive = !swap.active && actor.current == &swap && swap.CurrentHookStatus == 0 && !swap.bIsInCoolDown && actor.allowed;
    Check(nativeInactive, "observation never changes equipment, readiness, or native phase");
    std::ifstream saved(std::filesystem::path(savedPath) / L"Logs" / L"BRT-grapple-input.log");
    std::string line; std::vector<std::string> disk; while(std::getline(saved,line)) disk.push_back(line);
    Check(disk == records, "flushed player-side journal matches emitted records");
    for (int i=0; i<300; ++i) { step(0,0,0); event("K2_ResetHook"); }
    Check(records.size()==257 && records.back()=="[BRT-GRAPPLE-INPUT] event=limit", "one bounded file and explicit truncation marker");
    std::cout << checks << " checks passed; 0 gameplay writes\n";
}

#include "test-manager.hpp"
#include "engine-fixture.hpp"
#include <iostream>

using namespace SDK;
static bool candidate;
static void expect(bool ok, const char* description) {
    if (!ok) throw std::runtime_error(description);
}
struct Scene {
    bool matchStarted = true;
    std::unordered_map<APBPlayerController*, bool> allowed;
    LateJoinManager manager{matchStarted, allowed};
    APBPlayerState state;
    APBGameMode mode;
    APBPlayerController pc;
    APBCharacter original;
    ASpectatorPawn introCamera;
    Scene() {
        UWorld::GetWorld()->AuthorityGameMode = &mode;
        pc.PlayerState = &state;
        pc.view = &introCamera;
    }
    ~Scene() { UWorld::GetWorld()->AuthorityGameMode = nullptr; }
    void normalBirth(bool acknowledged) {
        state.selected = true;
        pc.Possess(&original);
        if (acknowledged) pc.AcknowledgedPawn = &original;
    }
    void intro() { manager.ForceFirstLifeSpawn(&pc); }
    void confirmRole() {
        state.selected = true;
        manager.OnRoleConfirmed(&pc);
    }
    void tick(float delta = 0.016f) { manager.Tick(delta); }
    void checkFinalDelivery() {
        expect(!manager.IsLateJoinPlayer(&pc), "finished player still queued");
        expect(pc.Pawn && pc.Pawn->Controller == &pc, "possession target");
        expect(pc.view == pc.Pawn, "camera target");
        expect(pc.AcknowledgedPawn == pc.Pawn, "acknowledged target");
    }
};

static void introBeforeRole() {
    Scene s;
    s.pc.Possess(&s.introCamera);
    s.intro();
    s.tick(1.1f);
    expect(s.mode.requests == (candidate ? 0 : 1), "spawn requests before selection");
    expect(s.pc.rolePrompts == (candidate ? 1 : 0), "role prompt before selection");
    if (candidate) expect(s.pc.Pawn == &s.introCamera && s.pc.view == &s.introCamera, "premature handoff");
    s.tick();
    s.confirmRole();
    s.tick();
    s.tick();
    s.checkFinalDelivery();
    expect(s.mode.requests == 1 && s.pc.clientRestarts == 1, "final single spawn");
}
static void normalBirth(bool acknowledged) {
    Scene s;
    s.normalBirth(acknowledged);
    const auto* left = &s.pc.GetPBCharacter()->left;
    const auto* right = &s.pc.GetPBCharacter()->right;
    s.intro();
    s.tick();
    s.tick();
    s.checkFinalDelivery();
    auto* pawn = s.pc.GetPBCharacter();
    expect(pawn != nullptr && (pawn == &s.original) == candidate, "role identity");
    expect((&pawn->left == left) == candidate && (&pawn->right == right) == candidate, "attachment identities");
    expect(s.mode.requests == (candidate ? 0 : 1), "additional spawn requests");
    expect(s.pc.cameraCalls == (candidate ? 1 : 0), "explicit camera handoff");
    const int expectedConfirmations = (acknowledged && candidate) ? 0 : 1;
    expect(s.pc.clientRestarts == expectedConfirmations && s.pc.confirmations == expectedConfirmations, "possession handshake count");
    if (expectedConfirmations) expect(s.pc.lastRetry == pawn, "retry target");
}
static void repeatWhileWaiting() {
    Scene s;
    s.state.selected = true;
    s.mode.spawnSucceeds = false;
    s.intro();
    s.tick();
    s.tick(1.4f);
    s.intro();
    s.tick(0.7f);
    expect(s.mode.requests == (candidate ? 1 : 2), "repeat restarted first attempt");
    expect(s.pc.quickRespawns == (candidate ? 1 : 0), "retry timer interrupted");
}
static void repeatAfterDelivery() {
    Scene s;
    s.normalBirth(true);
    s.intro();
    s.tick();
    s.tick();
    auto* first = s.pc.Pawn;
    s.intro();
    s.tick();
    s.tick();
    s.checkFinalDelivery();
    expect((s.pc.Pawn == first) == candidate, "second signal changed delivered role");
    expect(s.mode.requests == (candidate ? 0 : 2), "second signal spawn count");
    expect(s.pc.clientRestarts == (candidate ? 0 : 2), "second signal handshake count");
}
static void ordinaryJoin() {
    Scene s;
    s.normalBirth(true);
    expect(s.manager.OnPostLogin(&s.mode, &s.pc), "mid-match registration");
    s.confirmRole();
    s.tick();
    s.tick();
    s.checkFinalDelivery();
    expect(s.mode.requests == 1 && s.pc.Pawn != &s.original, "ordinary restart skipped");
    expect(s.pc.clientRestarts == 1 && s.pc.cameraCalls == 0, "ordinary restart delivery");
}
static void ordinaryRetry() {
    Scene s;
    s.mode.spawnSucceeds = false;
    expect(s.manager.OnPostLogin(&s.mode, &s.pc), "mid-match registration");
    s.confirmRole();
    s.tick();
    s.tick(2.1f);
    s.tick(2.1f);
    s.tick(2.1f);
    expect(s.mode.requests == 1 && s.pc.quickRespawns == 1 && s.pc.suicides == 1, "ordinary retry sequence");
    expect(!s.manager.IsLateJoinPlayer(&s.pc) && s.pc.cameraCalls == 0, "ordinary timeout cleanup");
}
static void ineligible(const std::string& kind) {
    Scene s;
    AController other;
    s.normalBirth(true);
    if (kind == "dead") s.original.alive = false;
    else if (kind == "killed") s.original.killed = true;
    else if (kind == "foreign") s.original.Controller = &other;
    else s.pc.Possess(&s.introCamera);
    s.intro();
    s.tick();
    s.tick();
    s.checkFinalDelivery();
    expect(s.mode.requests == 1 && s.pc.Pawn == &s.mode.generated.front(), "ineligible role reused");
}

int main(int argc, char** argv) {
    if (argc != 2 || (std::string(argv[1]) != "baseline" && std::string(argv[1]) != "candidate")) return 2;
    candidate = std::string(argv[1]) == "candidate";
    int failed = 0;
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            std::cerr << "FAIL " << name << " - " << error.what() << '\n';
            ++failed;
        }
    };
    run("intro before role selection", introBeforeRole);
    run("normal birth already acknowledged", [] { normalBirth(true); });
    run("normal birth awaiting acknowledgement", [] { normalBirth(false); });
    run("repeated intro during retry interval", repeatWhileWaiting);
    run("repeated intro after delivery", repeatAfterDelivery);
    run("ordinary mid-match restart", ordinaryJoin);
    run("ordinary retry fallbacks and timeout", ordinaryRetry);
    for (const char* kind : {"dead", "killed", "foreign", "spectator"}) {
        run(kind, [&] { ineligible(kind); });
    }
    return failed ? 1 : 0;
}

#include "BoundaryRound.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace brt::round;
static int Checks = 0;
#define CHECK(test) do { ++Checks; if (!(test)) { std::cerr << "FAIL " << __LINE__ << " " << #test << std::endl; std::exit(1); } } while (false)

struct PC {
    bool local = false, allow = true;
    int opens = 0;
    int exits = 0, toggles = 0, closes = 0;
    std::vector<std::string> submits;
    bool IsLocalController() const { return local; }
    bool CanSelectRole() { return allow; }
    void OpenInGameSelectRoleMenu() { ++opens; }
    void CloseInGameSelectRoleMenu() { ++closes; }
    void ToggleInGameSelectRoleMenu() { ++toggles; }
    void OpenExitMatchInGameMenu() { ++exits; }
    void ConfirmRoleSelection(std::string role) { submits.push_back(role); }
};
struct Rules {
    bool playing = true;
    std::set<PC*> eligible;
    bool IsRoundInProgress() const { return playing; }
    bool CanPlayerSelectRole(PC* pc) { return eligible.count(pc) != 0; }
};
using Server = RoundServer<PC, int>;
using Client = DeathMenu<PC, std::string>;

void TestNativeInputBinding() {
    PC owner;
    owner.local = true;
    Client local(owner);
    Life life{4,1};
    bool focus = false, fieldOpen = false, stockExit = true;
    auto press = [&] {
        DispatchInGameMenu(owner, focus, fieldOpen, stockExit, [&] {
            return local.Press(owner, true, focus, fieldOpen, false) == KeyResult::Consumed;
        }, [&] {
            local.OnMenuDismissedOrOpenFailed();
            fieldOpen = false;
        });
    };
    CHECK(local.Receive(owner, {life,1,true,false,false}));
    press(); CHECK(owner.exits == 1 && owner.opens == 0);
    focus = true;
    press(); CHECK(owner.exits == 1 && owner.toggles == 0);
    focus = false;
    stockExit = false;
    press(); CHECK(owner.toggles == 1);
    stockExit = true;
    CHECK(local.Receive(owner,{life,2,true,true,true}));
    press(); CHECK(owner.opens == 1 && owner.exits == 1);
    press(); CHECK(owner.opens == 1 && owner.exits == 1);
    fieldOpen = true;
    press(); CHECK(owner.closes == 1 && !fieldOpen && owner.exits == 1);
    press(); CHECK(owner.opens == 2);
    CHECK(local.Confirm(owner, life, "LEEWAY", true));
    CHECK(owner.submits == std::vector<std::string>{"LEEWAY"});
    CHECK(local.Receive(owner, {{4,2},1,true,false,false}));
    press(); CHECK(owner.exits == 2 && owner.opens == 2);
}

void TestOwnerAndDeath() {
    PC serverA, serverB, localA, localB;
    localA.local = localB.local = true;
    Server a(serverA), b(serverB);
    Client menuA(localA), menuB(localB);
    Rules rules;
    rules.eligible.insert(&serverA);
    const Life one{9, 1}, two{9, 2};
    auto deliver = [&](PC& recipient, SelectionState packet) {
        if (&recipient == &serverA) CHECK(menuA.Receive(localA, packet));
        else if (&recipient == &serverB) CHECK(menuB.Receive(localB, packet));
        else CHECK(false);
    };
    CHECK(a.Spawn(serverA, one, 101, rules, deliver));
    CHECK(b.Spawn(serverB, one, 201, rules, deliver));
    CHECK(!a.Spawn(serverB, two, 202, rules, deliver));
    CHECK(!menuA.OffersRole(localA, true));
    CHECK(menuA.Press(localA, true, false, false, false) == KeyResult::PassToExistingMenu);
    CHECK(!a.FinalDeath(&serverB, one, 201, rules, deliver));
    CHECK(!a.FinalDeath(&serverA, one, 999, rules, deliver));
    CHECK(a.FinalDeath(&serverA, one, 101, rules, deliver));
    const auto delayedDeath = a.State();
    CHECK(!a.FinalDeath(&serverA, one, 101, rules, deliver));
    CHECK(menuA.OffersRole(localA, true));
    CHECK(!menuB.OffersRole(localB, true));
    CHECK(!menuA.OffersRole(localB, true));
    CHECK(!menuA.OffersRole(localA, false));
    CHECK(menuA.Press(localA, true, true, false, false) == KeyResult::PassToExistingMenu);
    CHECK(menuA.Press(localA, true, false, false, true) == KeyResult::Consumed);
    CHECK(localA.opens == 0);
    CHECK(menuA.Press(localA, true, false, false, false) == KeyResult::Consumed);
    CHECK(menuA.Press(localA, true, false, false, false) == KeyResult::Consumed);
    CHECK(menuA.Press(localA, true, false, true, false) == KeyResult::Consumed);
    CHECK(localA.opens == 1 && localB.opens == 0);
    CHECK(menuA.Confirm(localA, one, "PROBE", true));
    CHECK(!menuA.Confirm(localA, one, "PROBE", true));
    CHECK(localA.submits == std::vector<std::string>{"PROBE"});
    menuA.OnSelectionResolved(one);
    menuA.OnMenuDismissedOrOpenFailed();
    CHECK(menuA.Press(localA, true, false, false, false) == KeyResult::Consumed);
    CHECK(localA.opens == 2);
    CHECK(!menuA.Confirm(localB, one, "PROBE", true));
    localA.allow = false;
    CHECK(!menuA.Confirm(localA, one, "PROBE", true));
    CHECK(!menuA.OffersRole(localA, true));
    localA.allow = true;
    rules.eligible.clear();
    CHECK(a.Refresh(one, rules, deliver));
    CHECK(!menuA.Confirm(localA, one, "PROBE", true));
    CHECK(!menuA.OffersRole(localA, true));
    CHECK(!menuA.Receive(localA, delayedDeath));
    CHECK(b.FinalDeath(&serverB, one, 201, rules, deliver));
    CHECK(!menuB.OffersRole(localB, true));
    rules.eligible.insert(&serverB);
    CHECK(b.Refresh(one, rules, deliver));
    CHECK(menuB.OffersRole(localB, true));
    CHECK(a.Spawn(serverA, two, 102, rules, deliver));
    CHECK(!menuA.Receive(localA, delayedDeath));
    CHECK(!a.FinalDeath(&serverA, one, 101, rules, deliver));
    CHECK(!menuA.Confirm(localA, one, "PROBE", true));
    CHECK(localA.submits.size() == 1);
    CHECK(b.End(one.round, deliver));
    CHECK(!menuB.OffersRole(localB, true));
    CHECK(!b.Spawn(serverB, two, 202, rules, deliver));
    CHECK(b.Spawn(serverB, {10, 3}, 203, rules, deliver));
}

void TestDeadKeyRoutingWhileCooling() {
    PC pc;
    pc.local = true;
    Client menu(pc);
    const Life life{12,1};
    const auto at = Client::Clock::time_point{} + std::chrono::seconds(15);
    int displayed = 0;
    auto remember = [&] { ++displayed; };
    CHECK(menu.Receive(pc, {life,1,true,false,false}));
    CHECK(menu.Receive(pc, {life,2,true,true,false}));
    DispatchInGameMenu(pc, false, false, true, [&] {
        return menu.Press(pc, true, false, false, false, remember, at) == KeyResult::Consumed;
    }, [] {});
    CHECK(pc.exits == 0 && pc.opens == 0);
    CHECK(menu.HasPendingPress() && displayed == 0);
    CHECK(!menu.ResumePress(pc,true,false,false,remember,at));
    pc.allow = false;
    CHECK(menu.Receive(pc,{life,3,true,true,true}));
    CHECK(!menu.ResumePress(pc,true,false,false,remember,at+std::chrono::milliseconds(100)));
    pc.allow = true;
    CHECK(menu.ResumePress(pc,true,false,false,remember,at+std::chrono::milliseconds(200)));
    CHECK(pc.opens == 1 && displayed == 1 && !menu.HasPendingPress());
    CHECK(!menu.ResumePress(pc,true,false,false,remember,at+std::chrono::milliseconds(300)));
    CHECK(menu.Confirm(pc,life,"FORT",true) && pc.submits == std::vector<std::string>{"FORT"});

    // 每一行都由真实的按下开始。授权随后就绪，也不能跨越取消动作代开界面。
    for (int scenario=0; scenario != 8; ++scenario) {
        PC owner; owner.local = true;
        Client intent(owner);
        CHECK(intent.Receive(owner,{life,1,true,true,false}));
        auto press = [&](bool repeat) {return intent.Press(owner,true,false,false,repeat,[] {},at);};
        CHECK(press(false) == KeyResult::Consumed && intent.HasPendingPress());
        CHECK(press(true) == KeyResult::Consumed && intent.HasPendingPress());
        auto now = at + std::chrono::milliseconds(400);
        if (scenario == 0) CHECK(press(false) == KeyResult::Consumed);
        if (scenario == 1) now = at + std::chrono::seconds(3);
        if (scenario == 2) intent.OnMenuDismissedOrOpenFailed();
        CHECK(intent.Receive(owner,{life,2,true,true,true}));
        if (scenario == 3) CHECK(intent.Receive(owner,{{12,2},1,true,false,false}));
        if (scenario == 4) CHECK(intent.Receive(owner,{life,3,false,true,false}));
        if (scenario == 5) CHECK(intent.Receive(owner,{life,3,true,true,false}));
        CHECK(!intent.ResumePress(owner,true,scenario == 6,scenario == 7,[] {},now));
        CHECK(!intent.HasPendingPress() && owner.opens == 0 && owner.exits == 0);
    }
}

enum class Camp { Friend, Enemy, Neutral };
enum class Kind { Character, Projectile };
struct Info {
    Camp CampValue = Camp::Enemy;
    Kind MarkableType = Kind::Character;
    bool bMarked = true, bIsLocal = false;
    bool bWeaponFired = false, bMobilityHeat = false;
    int Texture = 1;
    struct { float X = 0, Y = 0; } position;
    using CampType = ::Camp;
    CampType Camp = CampValue;
};
struct Target { int id; };
struct Widget {
    bool attached = false;
    int updates = 0;
    Info last{};
    void* GetParent() { return attached ? this : nullptr; }
    void RemoveFromParent() { attached = false; }
    void UpdateNode(const Info& value) { last = value; ++updates; }
};
struct MapHud {
    std::vector<std::pair<Target*, Info>> capture;
    int reads = 0;
    auto GetLastCaptureInfoNodeMap() { ++reads; return capture; }
};
struct Iter {
    template<class Map, class Visit> static void Each(const Map& source, Visit visit) {
        for (const auto& pair : source) visit(pair.first, pair.second);
    }
};
struct Factory {
    std::vector<std::unique_ptr<Widget>> all;
    bool attach = true;
    bool ValidTarget(Target* target) const { return target && target->id > 0; }
    Target* WeakTarget(Target* target) { return target; }
    Widget* WeakNode(Widget* node) { return node; }
    Widget* Resolve(Widget* node) { return node; }
    Widget* MakeNode() { all.push_back(std::make_unique<Widget>()); return all.back().get(); }
    void AddToNativeCanvas(Widget* node) { node->attached = attach; }
    int Visible() const {
        return static_cast<int>(std::count_if(all.begin(), all.end(), [](auto& node) { return node->attached; }));
    }
};
struct Approved {
    std::set<Target*> eligible, drawn;
    bool Eligible(Target* target) const { return eligible.count(target) != 0; }
    bool Drawn(Target* target) const { return drawn.count(target) != 0; }
};
void TestActualDrawCalls() {
    MapHud hud;
    Factory widgets;
    Approved sets;
    using Surface = NativeRadarSurface<Camp, Kind, MapHud, Target, Widget, Target*, Widget*, Iter, Factory, Approved>;
    Surface surface(hud, widgets, sets);
    MarkedRadar<Target*, Widget*> radar;
    const RadarView viewA{{9,1},1}, viewB{{9,2},2};
    radar.Attach(viewA, surface);
    auto draw = [&](RadarView view, int serial) {
        return radar.Finish(view, serial, [&](auto visit) { surface.VisitCapture(visit); }, surface);
    };
    Target direct{1}, hidden{2}, lockedMe{3}, friendTarget{4}, local{5}, projectile{6}, noTexture{7}, nan{8};
    Target firing{9}, hot{10};
    Info good;
    good.position.X = 0.25f;
    auto inverse = good; inverse.bMarked = false;
    auto onlyFire = inverse; onlyFire.bWeaponFired = true;
    auto onlyHeat = inverse; onlyHeat.bMobilityHeat = true;
    auto ally = good; ally.Camp = Camp::Friend;
    auto own = good; own.bIsLocal = true;
    auto gadget = good; gadget.MarkableType = Kind::Projectile;
    auto missing = good; missing.Texture = 0;
    auto invalid = good; invalid.position.X = std::numeric_limits<float>::quiet_NaN();
    hud.capture = {{&direct,good},{&direct,good},{&hidden,good},{&lockedMe,inverse},
        {&friendTarget,ally},{&local,own},{&projectile,gadget},{&noTexture,missing},{&nan,invalid},
        {&firing,onlyFire},{&hot,onlyHeat}};
    for (auto& entry : hud.capture) if (entry.first != &hidden) sets.eligible.insert(entry.first);
    CHECK(draw(viewA,1) && widgets.Visible() == 1 && widgets.all.size() == 1);
    CHECK(widgets.all.front()->updates == 1 && widgets.all.front()->last.position.X == 0.25f);
    CHECK(!draw(viewA,1) && hud.reads == 1);
    hud.capture.front().second.position.X = 0.55f;
    CHECK(draw(viewA,2) && widgets.all.front()->last.position.X == 0.55f);
    sets.drawn.insert(&direct);
    CHECK(draw(viewA,3) && widgets.Visible() == 0);
    sets.drawn.clear();
    CHECK(draw(viewA,4) && widgets.Visible() == 1);
    hud.capture.clear();
    CHECK(draw(viewA,5) && widgets.Visible() == 0);
    hud.capture = {{&direct,good}};
    widgets.attach = false;
    CHECK(draw(viewA,6) && widgets.Visible() == 0);
    widgets.attach = true;
    CHECK(draw(viewA,7) && widgets.Visible() == 1);
    radar.Attach(viewB, surface);
    CHECK(widgets.Visible() == 0 && !draw(viewA,8));
    CHECK(draw(viewB,1) && widgets.Visible() == 1);
    radar.Detach(surface);
    CHECK(widgets.Visible() == 0 && !draw(viewB,2));
}
int main() {
    TestNativeInputBinding();
    TestDeadKeyRoutingWhileCooling();
    TestOwnerAndDeath();
    std::cout << "PASS owning connection, final death, menu, confirmation dispatch, role rules and stale life" << std::endl;
    TestActualDrawCalls();
    std::cout << "PASS local captures, native widget, original node handover, loss of target and view change" << std::endl;
    std::cout << "PASS " << Checks << " assertions" << std::endl;
}

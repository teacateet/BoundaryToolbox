#include "OwnerBirthGate.hpp"
#include <stdexcept>
#include <cstdio>
using namespace brt::owner_birth;
void expect(bool value) { if (!value) throw std::runtime_error("owner birth contract"); }
int main() {
    Sample s{true,false,true,true,true,false,false,false,false,false,4,5,12,12,3,3,false,false,false,false};
    expect(Inspect(s).action == Action::Wait);
    s.linkedController = true; expect(Inspect(s).action == Action::Wait);
    s.alive = true; expect(Inspect(s).action == Action::Wait);
    s.roleSelected = true; expect(Inspect(s).action == Action::Ready);
    s.paused = true; expect(Inspect(s).action == Action::Wait);
    s.acknowledged = true; expect(Inspect(s).action == Action::Stop);
    s.acknowledged = false; s.paused = false; s.deadlinePassed = true;
    expect(Inspect(s).action == Action::Stop);
    s.deadlinePassed = false; s.bullets++; expect(Inspect(s).action == Action::Stop);
    s.bullets--; s.wrongController = true; expect(Inspect(s).action == Action::Stop);
    s.wrongController = false; s.sameStateAndTransport = false; expect(Inspect(s).action == Action::Stop);
    expect(QualifiedGun("SNIPER", "gsw-fop") == "SNIPER_GSW-FOP");
    expect(QualifiedGun("SNIPER", "Sniper_GSW-FOP") == "SNIPER_GSW-FOP");
    expect(QualifiedGun("SNIPER", "PEACE_GSW-FOP").empty());
    expect(QualifiedGun("PEACE", "None").empty() && QualifiedGun("PEACE", "").empty());
    expect(QualifiedGun("SPIKE", "GSW-PSR-C") == "SPIKE_GSW-PSR-C");
    std::puts("owner birth transitions and role-scoped weapon IDs passed");
}

#include "Timeline.hpp"
#include <cstdio>
#include <stdexcept>

using namespace brt::grapple;

struct Fixture {
    CooldownDelivery gate{};
    bool route = true;
    unsigned routeChecks = 0, sent = 0, missing = 0;
    void Tick(unsigned phase, bool cooling, int fraction = 1000) {
        auto action = gate.Observe({phase, cooling, phase == 2, fraction}, [&] {
            ++routeChecks;
            return route;
        });
        if (action == Delivery::NotifyOwner) ++sent;
        if (action == Delivery::WaitForOwner) ++missing;
    }
};

void Check(bool good, const char* why) {
    if (!good) throw std::runtime_error(why);
}

int main() {
    // OSS 第四条命：相邻帧直接从锁定转为待命；8‰ 时还在冷却。
    Fixture oss;
    oss.Tick(0, false); oss.Tick(1, false); oss.Tick(2, false);
    oss.Tick(0, true); oss.Tick(0, true, 8);
    Check(oss.routeChecks == 0 && oss.sent == 0, "never send during native cooling");
    oss.Tick(0, false); oss.Tick(0, false); oss.Tick(0, false);
    Check(oss.sent == 1 && oss.routeChecks == 1, "send exactly once when native cooling ends");
    oss.Tick(1, false); oss.Tick(2, false); oss.Tick(3, false); oss.Tick(0, true); oss.Tick(0, false);
    Check(oss.sent == 2, "next completed use gets its own notification");

    Fixture baseline;
    baseline.Tick(0, true); baseline.Tick(0, false); baseline.Tick(0, false);
    Check(baseline.routeChecks == 0, "joining during cooldown must not arm a repair");
    baseline.Tick(1, false); baseline.Tick(0, false);
    Check(baseline.sent == 0, "waiting for native cooldown onset after an unsuccessful shot");

    Fixture delayed;
    delayed.route = false;
    delayed.Tick(2, false); delayed.Tick(0, true); delayed.Tick(0, false); delayed.Tick(0, false);
    Check(delayed.sent == 0 && delayed.missing == 1, "missing owner is recorded once without sending");
    delayed.route = true; delayed.Tick(0, false); delayed.Tick(0, false);
    Check(delayed.sent == 1, "only the still-current cooldown can resume when ownership resolves");
    delayed.route = false; delayed.Tick(2, false); delayed.Tick(0, true); delayed.Tick(0, false);
    delayed.route = true; delayed.Tick(1, false); delayed.Tick(0, false);
    Check(delayed.sent == 1, "starting a new use discards the previous pending notification");
    delayed.route = false; delayed.Tick(2, false); delayed.Tick(0, true); delayed.Tick(0, false);
    delayed.gate = {}; delayed.route = true; delayed.Tick(0, false);
    Check(delayed.sent == 1, "replacing owner, pawn or equipment discards outstanding notification");
    std::puts("9 cooldown delivery checks passed");
}

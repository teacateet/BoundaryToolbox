#include "PreorderGate.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <iostream>

using namespace brt::preorder;
struct Key { int pc, wire, state; bool operator==(const Key&) const = default; };
using TestGate = Gate<Key, std::string>;
const Key owner{1, 2, 3};
const std::array<std::string, 6> savedRoles{"PEACE", "PROBE", "FIXER", "SNIPER", "FORT", "SPIKE"};
const Availability ready{true, true, true, false};

int main() {
    unsigned cases = 0;
    auto run = [&](TestGate& gate, Availability availability, std::vector<std::size_t>& calls, Key key = owner) {
        return gate.Tick(key, availability, [&](std::size_t i) { calls.push_back(i); return true; });
    };
    {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Queue(owner, savedRoles);
        assert(!gate.Observe(owner, {}, 0));
        assert(run(gate, ready, calls) == Progress::Waiting && calls.empty()); ++cases;
        assert(gate.Observe(owner, {savedRoles.begin(), savedRoles.end()}, 6));
        assert(run(gate, {true, false, true, false}, calls) == Progress::Waiting && calls.empty()); ++cases;
        assert(run(gate, {true, true, false, false}, calls) == Progress::Waiting && calls.empty()); ++cases;
        assert(run(gate, ready, calls) == Progress::Complete && calls.size() == 6); ++cases;
        assert(run(gate, ready, calls) == Progress::Idle && calls.size() == 6); ++cases;
    }
    {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Observe(owner, {savedRoles.begin(), savedRoles.end()}, 6);
        gate.Queue(owner, savedRoles);
        assert(run(gate, ready, calls) == Progress::Complete && calls.size() == 6); ++cases;
    }
    {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Queue(owner, savedRoles);
        gate.Observe(owner, {"PROBE"}, 1);
        assert(run(gate, ready, calls) == Progress::Waiting && calls == std::vector<std::size_t>{1}); ++cases;
        assert(run(gate, ready, calls) == Progress::Waiting && calls.size() == 1); ++cases;
        gate.Observe(owner, {savedRoles.begin(), savedRoles.end()}, 6);
        assert(run(gate, ready, calls) == Progress::Complete && calls.size() == 6); ++cases;
    }
    for (auto status : {Availability{true, true, true, true}, Availability{false, true, true, false}}) {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Queue(owner, savedRoles); gate.Observe(owner, {savedRoles.begin(), savedRoles.end()}, 6);
        assert(run(gate, status, calls) == Progress::Cancelled && calls.empty()); ++cases;
    }
    {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Queue(owner, savedRoles); gate.Observe({1, 2, 4}, {savedRoles.begin(), savedRoles.end()}, 6);
        assert(run(gate, ready, calls) == Progress::Waiting && calls.empty()); ++cases;
        assert(run(gate, ready, calls, {1, 2, 4}) == Progress::Cancelled && calls.empty()); ++cases;
    }
    {
        TestGate gate; std::vector<std::size_t> calls;
        gate.Queue(owner, savedRoles); gate.Observe(owner, {savedRoles.begin(), savedRoles.end()}, 6);
        assert(!gate.Observe(owner, {"PEACE", "PEACE"}, 2));
        assert(run(gate, ready, calls) == Progress::Waiting && calls.empty()); ++cases;
        assert(!gate.Observe(owner, {"PEACE"}, 0));
        assert(run(gate, ready, calls) == Progress::Waiting && calls.empty()); ++cases;
    }
    std::cout << "PASS " << cases << " preorder readiness scenarios\n";
}

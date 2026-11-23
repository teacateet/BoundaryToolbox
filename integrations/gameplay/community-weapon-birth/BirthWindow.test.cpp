#include "BirthWindow.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace brt::birth;
    Window window;
    const Counts counts{2, 18, 1};
    int pawn = 1, other = 2;
    const auto unused = [&](Counts current, float age, bool ack = false, bool firing = false) {
        return window.Unused(current, age, true, true, ack, firing, false);
    };
    assert(!window.Start(&pawn, 0.0f, 8.0f));
    window.Wait(counts, 8.0f);
    assert(window.Start(&pawn, 0.015625f, 8.03125f));
    assert(window.Owns(&pawn, 0.015625f) && unused(counts, 0.015625f));
    // 首帧为空、下一帧出现副枪；同一条生命不再次开始。
    assert(!window.Start(&pawn, 0.046875f, 8.0625f));
    assert(window.Owns(&pawn, 0.046875f) && unused(counts, 0.046875f));
    assert(FreshWeapon(0.015625f, true, false, false, false, true, false, 0.046875f));
    assert(!FreshWeapon(0.10f, true, false, false, false, true, false, 0.046875f));
    assert(!FreshWeapon(0.0f, true, true, false, false, true, false, 0.046875f));
    assert(!window.Owns(&other, 0.03125f) && !window.Owns(&pawn, 0.251f));
    assert(!unused(counts, 0.0625f, true) && !unused(counts, 0.0625f, false, true));
    assert(!unused({2,19,1}, 0.0625f) && !unused({2,18,2}, 0.0625f));
    window.Finish();
    assert(!window.Pending() && !window.Start(&pawn, 0.1f, 8.125f));
    window.Wait(counts, 9.0f);
    assert(window.Start(&pawn, 0.125f, 9.03125f) && !window.Pending());
    window.Wait(counts, 10.0f);
    assert(window.Start(&other, 0.0f, 10.0f) && window.Owns(&other, 0.0f));
    window.Wait(counts, 10.5f);
    assert(!window.Pending());

    const std::array<int, 2> both{1, 2}, primary{1, 0}, secondary{0, 2}, empty{};
    for (unsigned mask = 0; mask != 4; ++mask) {
        unsigned seen = 0;
        auto check = [&](unsigned i, int gun) {
            assert(gun == int(i + 1));
            seen |= 1u << i;
            return (mask & (1u << i)) != 0;
        };
        assert(VerifyOccupied(both, check) == (mask == 3) && seen == 3);
        seen = 0; assert(VerifyOccupied(primary, check) == ((mask & 1u) != 0) && seen == 1);
        seen = 0; assert(VerifyOccupied(secondary, check) == ((mask & 2u) != 0) && seen == 2);
        seen = 0; assert(!VerifyOccupied(empty, check) && seen == 0);
    }
    std::puts("PASS: delayed birth, split inventory, stale or active owner, per-slot readback matrix");
}

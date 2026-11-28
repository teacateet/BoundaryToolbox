#include "../RespawnWindow.hpp"
#include <cassert>
#include <limits>
int main() {
    using namespace brt::respawn_kit;
    assert(!InWindow(0,0) && InWindow(0,1) && InWindow(4.999f,2));
    assert(!InWindow(5,1) && !InWindow(12,3) && !InWindow(-0.01f,2));
    assert(!InWindow(std::numeric_limits<float>::quiet_NaN(),2));
    assert(CapAmmo(50,7)==7 && CapAmmo(6,7)==6 && CapAmmo(50,0)==0);
    assert(CapAmmo(50,CapAmmo(6,7))==6);
}

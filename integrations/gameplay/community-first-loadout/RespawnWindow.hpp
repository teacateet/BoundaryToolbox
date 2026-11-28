#pragma once
#include <algorithm>
#include <cmath>

// 使用服务端角色的出生年龄。再次确认、换枪或进入菜单均不能延长本命窗口。
namespace brt::respawn_kit {
inline bool InWindow(float age, int deaths) {
    return deaths > 0 && std::isfinite(age) && age >= 0.f && age < 5.f;
}
inline int CapAmmo(int fresh, int remaining) {
    return (std::min)((std::max)(0,fresh), (std::max)(0,remaining));
}
}

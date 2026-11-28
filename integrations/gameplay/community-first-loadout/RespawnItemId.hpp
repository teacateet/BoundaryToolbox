#pragma once
#include <string>
#include <string_view>

namespace brt::respawn_kit {
// 只处理角色范围已批准、但零售定义表使用无干员前缀行名的物品。
inline std::string ScopedDefinition(std::string_view role, std::string_view selected) {
    if (role.empty() || selected.size() <= role.size()+1 || !selected.starts_with(role)
        || selected[role.size()] != '_') return {};
    return std::string(selected.substr(role.size()+1));
}
}

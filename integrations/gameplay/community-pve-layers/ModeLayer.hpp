#pragma once
#include <string_view>

namespace brt::levels {
inline std::string_view ModeLayer(std::string_view mode) {
    if (mode == "BP_PBGameMode_Rush_PVE_Easy_C") return "Rush_PVE_Easy";
    if (mode == "BP_PBGameMode_Rush_PVE_Normal_C") return "Rush_PVE_Normal";
    if (mode == "BP_PBGameMode_Rush_PVE_Hard_C") return "Rush_PVE_Hard";
    return {};
}
inline std::string_view Layer(std::string_view path) {
    auto leaf = path.substr(path.find_last_of('/') + 1);
    auto package = leaf.substr(0, leaf.find('.'));
    auto cut = package.find("_MODE_");
    return cut == package.npos ? std::string_view{} : package.substr(cut + 6);
}
}

#include "OwnedBirth.boundary.hpp"
#include <windows.h>
#include <limits>

namespace brt::spawn::boundary {
namespace {
SDK::FName ToBoundaryName(const Name& text) {
    if (text.empty() || text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::invalid_argument("invalid Boundary item name");
    const int size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (count <= 0) throw std::invalid_argument("invalid UTF-8 item name");
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, wide.data(), count) != count)
        throw std::invalid_argument("invalid UTF-8 item name");
    return SDK::UKismetStringLibrary::Conv_StringToName(SDK::FString(wide.c_str()));
}
}

// 这些自由函数在同版本真实声明上实例化整个路径。真正玩法模块从自己的普通出生分支调用同一接口。
bool BeginOwnedBoundaryBirth(Birth& birth, std::uint64_t life, std::uint64_t generation,
                           const Selection& selection, SDK::APBCharacter& pawn,
                           const SDK::FPBRoleNetworkConfig& approvedRole) {
    return birth.Begin(life, generation, selection, pawn, approvedRole, ToBoundaryName);
}
bool PrepareOwnedBoundaryBirth(Birth& birth, std::uint64_t generation, std::string_view reply) {
    return birth.Prepare(generation, reply);
}
bool GetOwnedBoundaryInventory(Birth& birth, std::uint64_t life, std::uint64_t generation, SDK::APBCharacter& pawn,
    const SDK::FPBRoleNetworkConfig& approved, const SDK::FPBRoleNetworkConfig& factory,
    const SDK::UPBCharacterDefaultConfig& defaults, SDK::FPBRoleNetworkConfig& prepared) {
    // 不得以宿主的共享默认对象或当前批准对象作为输出缓冲区。
    if (&prepared == &approved || &prepared == &factory) return false;
    return birth.WithInventory(life,generation,pawn,approved,factory,defaults,ToBoundaryName,
        [&](const SDK::FPBRoleNetworkConfig& value){ prepared=value; return true; });
}
BirthInit InitializeOwnedBoundaryWeapon(Birth& birth, std::uint64_t life, std::uint64_t generation,
                                      SDK::APBCharacter& pawn, SDK::APBWeapon* primary,
                                      SDK::APBWeapon* secondary, SDK::APBWeapon& created) {
    return birth.InitCurrent(life, generation, pawn, primary, secondary, created, ToBoundaryName);
}
} // namespace brt::spawn::boundary

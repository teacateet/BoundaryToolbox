#pragma once

#include "OwnedBirth.hpp"
#include "SDK/ProjectBoundary_classes.hpp"

namespace brt::spawn::boundary {

using Birth = OwnedBirth<SDK::APBPlayerController, SDK::APBCharacter, SDK::APBWeapon>;

bool BeginOwnedBoundaryBirth(Birth& birth, std::uint64_t life, std::uint64_t generation,
                            const Selection& selection, SDK::APBCharacter& pawn,
                            const SDK::FPBRoleNetworkConfig& approvedRole);
bool PrepareOwnedBoundaryBirth(Birth& birth, std::uint64_t generation, std::string_view reply);
bool GetOwnedBoundaryInventory(Birth& birth, std::uint64_t life, std::uint64_t generation, SDK::APBCharacter& pawn,
    const SDK::FPBRoleNetworkConfig& approved, const SDK::FPBRoleNetworkConfig& factory,
    const SDK::UPBCharacterDefaultConfig& defaults, SDK::FPBRoleNetworkConfig& prepared);
BirthInit InitializeOwnedBoundaryWeapon(Birth& birth, std::uint64_t life, std::uint64_t generation,
                                       SDK::APBCharacter& pawn, SDK::APBWeapon* primary,
                                       SDK::APBWeapon* secondary, SDK::APBWeapon& created);

} // namespace brt::spawn::boundary

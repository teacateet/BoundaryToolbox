#pragma once

namespace BrtShoulders
{
struct Slots
{
    SDK::APBLauncher* left{};
    SDK::APBLauncher* right{};
    bool Contains(SDK::APBLauncher* pod) const { return pod && (pod == left || pod == right); }
};

inline Slots Of(SDK::APBCharacter* pawn)
{
    if (!pawn) return {};
    auto* pack = pawn->CurrentVehicle;
    if (SDK::UKismetSystemLibrary::IsValid(pack))
        return {pack->LeftLauncher, pack->RightLauncher};
    return {pawn->CurrentLeftLauncher, pawn->CurrentRightLauncher};
}
}

#pragma once
#include <array>
#include <string_view>
namespace brt::birth {
struct RolePath { std::string_view id, path; };
// Generated from all 18 rows of DT_CharacterDefinition.
inline constexpr std::array rolePaths{
    RolePath{"PEACE", "/Game/Gameplay/Character/PBCharacter_BP_Assault.PBCharacter_BP_Assault_C"},
    RolePath{"PROBE", "/Game/Gameplay/Character/PBCharacter_BP_Recon.PBCharacter_BP_Recon_C"},
    RolePath{"SNIPER", "/Game/Gameplay/Character/PBCharacter_BP_Sniper.PBCharacter_BP_Sniper_C"},
    RolePath{"FORT", "/Game/Gameplay/Character/PBCharacter_BP_Tank.PBCharacter_BP_Tank_C"},
    RolePath{"SPIKE", "/Game/Gameplay/Character/PBCharacter_BP_Assassin.PBCharacter_BP_Assassin_C"},
    RolePath{"FIXER", "/Game/Gameplay/Character/PBCharacter_BP_Medic.PBCharacter_BP_Medic_C"},
    RolePath{"SPIDER", "/Game/Gameplay/Character/PBCharacter_BP_Assassin.PBCharacter_BP_Assassin_C"},
    RolePath{"BILL", "/Game/Gameplay/Character/PBCharacter_BP_Assault.PBCharacter_BP_Assault_C"},
    RolePath{"AI_PEACE", "/Game/Gameplay/Character/PBCharacter_BP_Assault.PBCharacter_BP_Assault_C"},
    RolePath{"AI_PROBE", "/Game/Gameplay/Character/PBCharacter_BP_Recon.PBCharacter_BP_Recon_C"},
    RolePath{"AI_SNIPER", "/Game/Gameplay/Character/PBCharacter_BP_Sniper.PBCharacter_BP_Sniper_C"},
    RolePath{"AI_FORT", "/Game/Gameplay/Character/PBCharacter_BP_Tank.PBCharacter_BP_Tank_C"},
    RolePath{"AI_SPIKE", "/Game/Gameplay/Character/PBCharacter_BP_Assassin.PBCharacter_BP_Assassin_C"},
    RolePath{"AI_FIXER", "/Game/Gameplay/Character/PBCharacter_BP_Medic.PBCharacter_BP_Medic_C"},
    RolePath{"SPIDER_HC", "/Game/Characters/MobileArmors/BP_MobileArmor.BP_MobileArmor_C"},
    RolePath{"RECON-HC", "/Game/Gameplay/Character/PBCharacter_BP_Sniper.PBCharacter_BP_Sniper_C"},
    RolePath{"ASSAULT-HC", "/Game/Gameplay/Character/PBCharacter_BP_Assault.PBCharacter_BP_Assault_C"},
    RolePath{"SUPPORT-HC", "/Game/Gameplay/Character/PBCharacter_BP_Tank.PBCharacter_BP_Tank_C"}
};
}

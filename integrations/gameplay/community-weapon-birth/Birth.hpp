#pragma once
#include "OwnerRoleMenu.hpp"

namespace SDK { class APBPlayerController; }
namespace brt::spawn { class RoomHttp; }
bool BrtWatchOwnerBirth(SDK::APBPlayerController* pc, const brt::spawn::RoomHttp& room,
                        const std::string& steam, const std::vector<brt::spawn::SavedMenuRole>& rows) noexcept;
void BrtTickOwnerBirth() noexcept;

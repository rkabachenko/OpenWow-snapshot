
#pragma once

#include <cstdint>

namespace openwow::game {

class ObjectManager;

inline constexpr std::uint32_t kAuraConditionBit_PetEquipSuppressed = 273;

[[nodiscard]] bool HasActivePlayerPetEquipSuppressAura(
    const ObjectManager &obj_mgr);

}

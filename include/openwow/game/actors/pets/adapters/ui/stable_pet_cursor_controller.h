#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actors/pets/model/stable_pet_cursor_slot.h"

#include <cstdint>

namespace openwow::game {
class WorldSession;
}

namespace openwow::game::actors::pets::ui {

enum class StablePetCursorPickupResult : std::uint8_t {
  kPickedUp,
  kPetNotFound,
  kCreatureTemplateUnavailable,
  kCreatureFamilyUnavailable,
  kIconUnavailable,
};

[[nodiscard]] bool HasStablePetForCursor(
    const WorldSession& session, StablePetCursorSlot slot);

[[nodiscard]] StablePetCursorPickupResult PickupStablePetCursor(
    actions::held_cursor::HeldCursor& cursor, const WorldSession& session,
    StablePetCursorSlot slot);

}

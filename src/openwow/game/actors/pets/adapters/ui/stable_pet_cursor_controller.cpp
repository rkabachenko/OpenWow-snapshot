#include "openwow/game/actors/pets/adapters/ui/stable_pet_cursor_controller.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/world_session.h"
#include <string>
#include <string_view>

namespace openwow::game::actors::pets::ui {
namespace {

constexpr std::uint8_t kStabledPetFlag = 0x2u;

const StablePetEntry* FindPet(const StableListInfo& stable_list,
                              const StablePetCursorSlot slot) {
  if (slot.IsCurrentPet()) {
    for (const auto& pet : stable_list.pets) {
      if ((pet.flags & kStabledPetFlag) == 0) {
        return &pet;
      }
    }
    return nullptr;
  }

  std::uint32_t visible_index = 0;
  for (const auto& pet : stable_list.pets) {
    if ((pet.flags & kStabledPetFlag) == 0) {
      continue;
    }
    if (visible_index == *slot.stabled_index()) {
      return &pet;
    }
    ++visible_index;
  }
  return nullptr;
}

int ToLegacyHeldSlot(const StablePetCursorSlot slot) {
  if (slot.IsCurrentPet()) {
    return -1;
  }

  return static_cast<int>(*slot.stabled_index());
}

}

bool HasStablePetForCursor(const WorldSession& session,
                           const StablePetCursorSlot slot) {
  const auto* pet = FindPet(session.pet().stable_list(), slot);
  return pet != nullptr && pet->pet_number != 0;
}

StablePetCursorPickupResult PickupStablePetCursor(
    actions::held_cursor::HeldCursor& cursor, const WorldSession& session,
    const StablePetCursorSlot slot) {
  cursor.Clear();

  const auto* stable_pet = FindPet(session.pet().stable_list(), slot);
  if (stable_pet == nullptr || stable_pet->pet_number == 0 ||
      stable_pet->creature_id == 0) {
    return StablePetCursorPickupResult::kPetNotFound;
  }

  const auto* creature_template =
      session.query_cache().GetCreatureTemplate(stable_pet->creature_id);
  if (creature_template == nullptr || creature_template->creature_family == 0) {
    return StablePetCursorPickupResult::kCreatureTemplateUnavailable;
  }

  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return StablePetCursorPickupResult::kCreatureFamilyUnavailable;
  }

  const auto* family =
      dbc->creature_family().LookupEntry(creature_template->creature_family);
  if (family == nullptr) {
    return StablePetCursorPickupResult::kCreatureFamilyUnavailable;
  }
  if (std::string_view(family->icon_file).empty()) {
    return StablePetCursorPickupResult::kIconUnavailable;
  }

  cursor.HoldStablePet(
      actions::held_cursor::StablePet{
          .stable_index = ToLegacyHeldSlot(slot),
      },
      actions::held_cursor::Presentation{
          .texture_path = std::string(family->icon_file),
          .sound = actions::held_cursor::Sound::CursorGrabObject,
      });
  return StablePetCursorPickupResult::kPickedUp;
}

}

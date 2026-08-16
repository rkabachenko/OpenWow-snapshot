#pragma once

#include "openwow/game/object_presentation_snapshot.h"

#include <array>
#include <cstdint>

namespace openwow::game {

inline constexpr std::uint32_t kAmmoProjectileAttachmentId = 24u;
inline constexpr std::uint32_t kThrownProjectileAttachmentId = 25u;

struct EquippedItemPresentation {
  std::uint32_t item_id = 0;
  std::uint32_t display_id = 0;
  std::uint32_t permanent_enchantment = 0;
  std::uint32_t temporary_enchantment = 0;
  std::uint32_t item_visual = 0;
  std::uint8_t inventory_type = 0;
  std::uint8_t sheath_type = 0;
};

struct EquipmentPresentation {
  ObjectHandle owner;
  std::uint64_t inventory_revision = 0;
  std::array<EquippedItemPresentation, 19> slots{};
  std::uint32_t ammo_display_id = 0;
  std::uint32_t ammo_attachment_id = kAmmoProjectileAttachmentId;
};

}

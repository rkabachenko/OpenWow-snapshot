
#include "openwow/game/pet_handler.h"

#include "openwow/game/packet_reader.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::game {

bool PetHandler::HandlePetTameFailure(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t reason;
  if (!r.ReadU8(reason)) return false;
  last_tame_failure_ = reason;
  return true;
}

bool PetHandler::HandlePetNameInvalid(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  PetNameInvalidInfo info{};

  if (!r.ReadU32(info.error_code)) return false;
  if (!r.ReadCString(info.name)) return false;
  if (!r.ReadU8(info.has_declined)) return false;

  if (info.has_declined != 0) {
    for (int i = 0; i < 5; ++i) {
      if (!r.ReadCString(info.declined_names[i])) return false;
    }
  }

  pet_name_invalid_ = std::move(info);
  return true;
}

bool PetHandler::HandlePetBroken() {
  pet_broken_ = true;
  return true;
}

bool PetHandler::HandlePetActionSound(const std::uint8_t* data,
                                      std::size_t len) {
  PacketReader r(data, len);
  PetActionSound s{};
  if (!r.ReadU64(s.pet_guid)) return false;
  if (!r.ReadU32(s.sound_id)) return false;
  last_pet_action_sound_ = s;
  return true;
}

bool PetHandler::HandlePetDismissSound(const std::uint8_t* data,
                                       std::size_t len) {
  PacketReader r(data, len);
  PetDismissSound s{};
  if (!r.ReadU32(s.creature_display_id)) return false;
  if (!r.ReadFloat(s.x)) return false;
  if (!r.ReadFloat(s.y)) return false;
  if (!r.ReadFloat(s.z)) return false;
  last_pet_dismiss_sound_ = s;
  return true;
}

bool PetHandler::HandlePetUnlearnConfirm(const std::uint8_t* data,
                                         std::size_t len) {
  PacketReader r(data, len);
  PetUnlearnConfirm info{};
  if (!r.ReadU64(info.pet_guid)) return false;
  if (!r.ReadU32(info.cost)) return false;
  last_pet_unlearn_confirm_ = info;
  return true;
}

bool PetHandler::HandleStableResult(const std::uint8_t* data,
                                    std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU8(stable_result_)) return false;
  return true;
}

bool PetHandler::HandlePetRenameable() {
  pet_renameable_ = true;
  return true;
}

bool PetHandler::HandlePetUpdateComboPoints(const std::uint8_t* data,
                                            std::size_t len) {
  PacketReader r(data, len);
  PetComboPoints pts{};
  ObjectGuid target{ObjectGuid(0)};
  if (!r.ReadPackedGuid(target)) return false;
  pts.target_guid = target.GetRawValue();
  if (!r.ReadU8(pts.combo_points)) return false;
  last_pet_combo_points_ = pts;
  return true;
}

void PetHandler::Clear() {
  last_tame_failure_ = 0;
  pet_name_invalid_.reset();
  pet_broken_ = false;
  last_pet_action_sound_.reset();
  last_pet_dismiss_sound_.reset();
  last_pet_unlearn_confirm_.reset();
  stable_result_ = 0;
  pet_renameable_ = false;
  last_pet_combo_points_.reset();
}

}

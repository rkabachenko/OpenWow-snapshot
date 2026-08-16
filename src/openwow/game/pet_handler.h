
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct PetNameInvalidInfo {
  std::uint32_t error_code = 0;
  std::string name;
  std::uint8_t has_declined = 0;
  std::string declined_names[5];
};

struct PetActionSound {
  std::uint64_t pet_guid = 0;
  std::uint32_t sound_id = 0;
};

struct PetDismissSound {
  std::uint32_t creature_display_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct PetUnlearnConfirm {
  std::uint64_t pet_guid = 0;
  std::uint32_t cost = 0;
};

struct PetComboPoints {
  std::uint64_t target_guid = 0;
  std::uint8_t combo_points = 0;
};

class PetHandler {
 public:
  bool HandlePetTameFailure(const std::uint8_t* data, std::size_t len);
  bool HandlePetNameInvalid(const std::uint8_t* data, std::size_t len);
  bool HandlePetBroken();
  bool HandlePetActionSound(const std::uint8_t* data, std::size_t len);
  bool HandlePetDismissSound(const std::uint8_t* data, std::size_t len);
  bool HandlePetUnlearnConfirm(const std::uint8_t* data, std::size_t len);
  bool HandleStableResult(const std::uint8_t* data, std::size_t len);
  bool HandlePetRenameable();
  bool HandlePetUpdateComboPoints(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] std::uint8_t last_tame_failure() const { return last_tame_failure_; }
  [[nodiscard]] const std::optional<PetNameInvalidInfo>& pet_name_invalid() const {
    return pet_name_invalid_;
  }
  [[nodiscard]] bool pet_broken() const { return pet_broken_; }
  [[nodiscard]] const std::optional<PetActionSound>& last_pet_action_sound() const {
    return last_pet_action_sound_;
  }
  [[nodiscard]] const std::optional<PetDismissSound>& last_pet_dismiss_sound() const {
    return last_pet_dismiss_sound_;
  }
  [[nodiscard]] const std::optional<PetUnlearnConfirm>& last_pet_unlearn_confirm() const {
    return last_pet_unlearn_confirm_;
  }
  [[nodiscard]] std::uint8_t last_stable_result() const { return stable_result_; }
  [[nodiscard]] bool pet_renameable() const { return pet_renameable_; }
  [[nodiscard]] const std::optional<PetComboPoints>& last_pet_combo_points() const {
    return last_pet_combo_points_;
  }

  void Clear();

 private:
  std::uint8_t last_tame_failure_ = 0;
  std::optional<PetNameInvalidInfo> pet_name_invalid_;
  bool pet_broken_ = false;
  std::optional<PetActionSound> last_pet_action_sound_;
  std::optional<PetDismissSound> last_pet_dismiss_sound_;
  std::optional<PetUnlearnConfirm> last_pet_unlearn_confirm_;
  std::uint8_t stable_result_ = 0;
  bool pet_renameable_ = false;
  std::optional<PetComboPoints> last_pet_combo_points_;
};

}

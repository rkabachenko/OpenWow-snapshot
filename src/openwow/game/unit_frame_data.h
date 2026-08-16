#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/unit_defines.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::game {

class ObjectManager;

struct UnitFrameData {
  ObjectGuid guid;
  std::string name;
  std::uint8_t race = 0;
  std::uint8_t class_id = 0;
  std::uint8_t gender = 0;
  std::uint32_t level = 0;

  std::int32_t health = 0;
  std::int32_t max_health = 0;
  float health_percent = 0.0f;

  PowerType power_type = PowerType::kMana;
  std::int32_t power = 0;
  std::int32_t max_power = 0;
  float power_percent = 0.0f;

  std::int32_t rune_blood[2] = {};
  std::int32_t rune_unholy[2] = {};
  std::int32_t rune_frost[2] = {};
  float rune_cooldown[6] = {};
  std::int32_t combo_points = 0;
  std::int32_t soul_shards = 0;

  bool is_dead = false;
  bool is_ghost = false;
  bool is_connected = true;
  bool is_afk = false;
  bool is_dnd = false;
  bool in_combat = false;
  bool is_tapped = false;
  bool is_tapped_by_player = false;

  bool is_pvp = false;
  bool is_ffa = false;

  std::uint8_t reaction = 0;

  std::uint32_t display_id = 0;
  std::uint8_t classification = 0;
  std::string creature_type;

  std::uint32_t buff_count = 0;
  std::uint32_t debuff_count = 0;

  std::uint8_t role = 0;
  std::uint8_t subgroup = 0;

  float distance = 0.0f;
  bool in_range = true;

  bool has_data = false;
};

class UnitFrameDataProvider {
 public:
  static UnitFrameDataProvider& Get();

  void Update(const ObjectManager& objects);

  [[nodiscard]] const UnitFrameData& GetPlayerData() const;
  [[nodiscard]] const UnitFrameData& GetTargetData() const;
  [[nodiscard]] const UnitFrameData& GetFocusData() const;
  [[nodiscard]] const UnitFrameData& GetTargetOfTargetData() const;
  [[nodiscard]] const UnitFrameData& GetPetData() const;
  [[nodiscard]] const UnitFrameData& GetPartyData(std::uint32_t index) const;
  [[nodiscard]] const UnitFrameData& GetRaidData(std::uint32_t index) const;

  [[nodiscard]] const UnitFrameData& GetUnitData(
      const std::string& token) const;

  [[nodiscard]] bool HasTarget() const;
  [[nodiscard]] bool HasFocus() const;
  [[nodiscard]] bool HasPet() const;
  [[nodiscard]] bool HasPartyMember(std::uint32_t index) const;

  void SetPlayerData(const UnitFrameData& data);
  void SetTargetData(const UnitFrameData& data);
  void SetFocusData(const UnitFrameData& data);
  void SetTargetOfTargetData(const UnitFrameData& data);
  void SetPetData(const UnitFrameData& data);
  void SetPartyData(std::uint32_t index, const UnitFrameData& data);
  void SetRaidData(std::uint32_t index, const UnitFrameData& data);

  void Reset();

 private:
  UnitFrameDataProvider() = default;

  void UpdateUnitData(const ObjectManager& objects, const ObjectGuid& guid,
                      UnitFrameData& data);

  UnitFrameData player_;
  UnitFrameData target_;
  UnitFrameData focus_;
  UnitFrameData target_of_target_;
  UnitFrameData pet_;
  std::array<UnitFrameData, 4> party_{};
  std::array<UnitFrameData, 40> raid_{};
  UnitFrameData empty_data_;
  mutable std::mutex mutex_;
};

}

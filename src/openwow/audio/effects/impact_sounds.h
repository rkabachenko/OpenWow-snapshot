
#pragma once

#include "openwow/data/formats/dbc/dbc_table_registry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::audio {

inline constexpr std::uint32_t kWeaponImpactSoundType = 14;
inline constexpr std::uint32_t kWeaponImpactListenerPriority = 110;
inline constexpr std::uint32_t kWeaponImpactDefaultSlot = 6;
inline constexpr std::uint32_t kWeaponImpactWeaponSlotBase = 6;
inline constexpr std::uint32_t kWeaponImpactArmorSlotBase = 4;
inline constexpr float kWeaponImpactHeightBias = 2.0f;

struct WeaponImpactSoundPair {
  std::uint32_t hit_sound_kit_id{0};
  std::uint32_t crit_sound_kit_id{0};
};

struct WeaponImpactSoundRowData {
  static constexpr std::uint32_t kImpactSlotCount = 10;

  std::uint32_t weapon_subclass_id{0};
  std::uint32_t parry_type{0};
  std::array<std::uint32_t, kImpactSlotCount> impact_sound{};
  std::array<std::uint32_t, kImpactSlotCount> crit_impact_sound{};
};

struct WeaponImpactItemData {
  std::uint8_t item_class{0};
  std::uint8_t fallback_weapon_subclass_id{0};
  std::uint8_t weapon_subclass_id{0xFF};
  std::uint8_t material_id{0};
};

struct WeaponImpactSelection {
  std::uint32_t weapon_subclass_id{0};
  std::uint32_t parry_type{0};
  std::uint32_t impact_slot{kWeaponImpactDefaultSlot};
};

using WeaponImpactMaterialFlagQuery = std::function<bool(std::uint8_t material_id)>;

class WeaponImpactSoundTable {
public:
  static constexpr std::uint32_t kParryTypeCount = 2;
  static constexpr std::uint32_t kImpactSlotCount = WeaponImpactSoundRowData::kImpactSlotCount;

  void Load(const std::vector<WeaponImpactSoundRowData> &rows,
            const std::uint32_t item_subclass_count) {
    Resize(item_subclass_count);

    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      const WeaponImpactSoundRowData &row = *it;
      if (row.weapon_subclass_id >= item_subclass_count_ || row.parry_type >= kParryTypeCount) {
        continue;
      }

      for (std::uint32_t impact_slot = 0; impact_slot < kImpactSlotCount; ++impact_slot) {
        sound_pairs_[IndexOf(row.weapon_subclass_id, row.parry_type, impact_slot)] = {
            row.impact_sound[impact_slot],
            row.crit_impact_sound[impact_slot],
        };
      }
    }
  }

  void Reset() {
    sound_pairs_.clear();
    item_subclass_count_ = 0;
  }

  [[nodiscard]] std::uint32_t GetSoundKit(const std::uint32_t weapon_subclass_id,
                                          const std::uint32_t parry_type,
                                          const std::uint32_t impact_slot,
                                          const bool critical) const {
    if (weapon_subclass_id >= item_subclass_count_ || parry_type >= kParryTypeCount ||
        impact_slot >= kImpactSlotCount) {
      return 0;
    }

    const WeaponImpactSoundPair &pair =
        sound_pairs_[IndexOf(weapon_subclass_id, parry_type, impact_slot)];
    return critical ? pair.crit_sound_kit_id : pair.hit_sound_kit_id;
  }

  [[nodiscard]] std::uint32_t GetItemSubclassCount() const {
    return item_subclass_count_;
  }

  [[nodiscard]] std::size_t GetPairCount() const {
    return sound_pairs_.size();
  }

private:
  static constexpr std::size_t kPairsPerWeaponSubclass =
      static_cast<std::size_t>(kParryTypeCount) * kImpactSlotCount;

  void Resize(const std::uint32_t item_subclass_count) {
    if (item_subclass_count == 0) {
      Reset();
      return;
    }

    sound_pairs_.resize(static_cast<std::size_t>(item_subclass_count) * kPairsPerWeaponSubclass);
    item_subclass_count_ = item_subclass_count;
  }

  [[nodiscard]] static std::size_t IndexOf(const std::uint32_t weapon_subclass_id,
                                           const std::uint32_t parry_type,
                                           const std::uint32_t impact_slot) {
    return static_cast<std::size_t>(parry_type) +
           2u * (static_cast<std::size_t>(impact_slot) +
                 kImpactSlotCount * static_cast<std::size_t>(weapon_subclass_id));
  }

  std::vector<WeaponImpactSoundPair> sound_pairs_;
  std::uint32_t item_subclass_count_{0};
};

[[nodiscard]] inline std::uint32_t
ResolveWeaponImpactWeaponSubclassId(const WeaponImpactItemData *item,
                                    const std::uint32_t local_player_ranged_weapon_subclass_id) {
  if (item == nullptr) {
    return local_player_ranged_weapon_subclass_id;
  }

  return item->weapon_subclass_id == 0xFF ? item->fallback_weapon_subclass_id
                                          : item->weapon_subclass_id;
}

[[nodiscard]] inline std::uint32_t
ResolveWeaponImpactParryType(const WeaponImpactItemData *item,
                             const WeaponImpactMaterialFlagQuery &uses_parry_type_one) {
  if (item == nullptr || !uses_parry_type_one) {
    return 0;
  }

  return uses_parry_type_one(item->material_id) ? 1u : 0u;
}

[[nodiscard]] inline std::uint32_t
ResolveWeaponImpactSlotFromTargetItem(const WeaponImpactItemData *item,
                                      const WeaponImpactMaterialFlagQuery &uses_parry_type_one) {
  if (item == nullptr) {
    return kWeaponImpactDefaultSlot;
  }

  if (item->item_class == 2) {
    const std::uint32_t material_variant =
        uses_parry_type_one && uses_parry_type_one(item->material_id) ? 1u : 0u;
    return kWeaponImpactWeaponSlotBase - material_variant;
  }

  if (item->item_class == 4) {
    const std::uint32_t material_variant =
        uses_parry_type_one && uses_parry_type_one(item->material_id) ? 1u : 0u;
    return kWeaponImpactArmorSlotBase - material_variant;
  }

  return kWeaponImpactDefaultSlot;
}

[[nodiscard]] inline WeaponImpactSelection
ResolveWeaponImpactSelectionFromEquipment(const WeaponImpactItemData *attacker_item,
                                          const WeaponImpactItemData *target_item,
                                          const std::uint32_t local_player_ranged_weapon_subclass_id,
                                          const WeaponImpactMaterialFlagQuery &uses_parry_type_one) {
  return {
      ResolveWeaponImpactWeaponSubclassId(attacker_item, local_player_ranged_weapon_subclass_id),
      ResolveWeaponImpactParryType(attacker_item, uses_parry_type_one),
      ResolveWeaponImpactSlotFromTargetItem(target_item, uses_parry_type_one),
  };
}

[[nodiscard]] inline WeaponImpactSelection
ResolveWeaponImpactSelectionFromEquipment(const WeaponImpactItemData *attacker_item,
                                          const WeaponImpactItemData *target_item,
                                          const std::uint32_t local_player_ranged_weapon_subclass_id) {
  return ResolveWeaponImpactSelectionFromEquipment(
      attacker_item, target_item, local_player_ranged_weapon_subclass_id,
      [](const std::uint8_t material_id) {
        return openwow::data::DBClient_MaterialUsesWeaponImpactParryTypeOne(material_id);
      });
}

[[nodiscard]] inline WeaponImpactSelection
ResolveWeaponImpactSelectionForImpactSlot(const WeaponImpactItemData *attacker_item,
                                          const std::uint32_t impact_slot,
                                          const std::uint32_t local_player_ranged_weapon_subclass_id,
                                          const WeaponImpactMaterialFlagQuery &uses_parry_type_one) {
  return {
      ResolveWeaponImpactWeaponSubclassId(attacker_item, local_player_ranged_weapon_subclass_id),
      ResolveWeaponImpactParryType(attacker_item, uses_parry_type_one),
      impact_slot,
  };
}

[[nodiscard]] inline WeaponImpactSelection
ResolveWeaponImpactSelectionForImpactSlot(const WeaponImpactItemData *attacker_item,
                                          const std::uint32_t impact_slot,
                                          const std::uint32_t local_player_ranged_weapon_subclass_id) {
  return ResolveWeaponImpactSelectionForImpactSlot(
      attacker_item, impact_slot, local_player_ranged_weapon_subclass_id,
      [](const std::uint8_t material_id) {
        return openwow::data::DBClient_MaterialUsesWeaponImpactParryTypeOne(material_id);
      });
}

}

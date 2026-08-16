#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace openwow::game::item_scaling {

inline constexpr std::uint32_t kScalingStatBudgetMask = 0x4001Fu;
inline constexpr std::uint32_t kScalingArmorMask = 0xF801E0u;
inline constexpr std::uint32_t kScalingWeaponMask = 0x7E00u;
inline constexpr std::uint32_t kScalingSpellPowerFlag = 0x8000u;

struct ScalingWeaponDpsInfo {
  float base_dps = 0.0f;
  float min_dps = 0.0f;
  float max_dps = 0.0f;
};

template <typename ItemT>
inline const openwow::data::dbc::ScalingStatDistributionEntry*
FindScalingStatDistribution(const ItemT& item,
                            const openwow::data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr || item.scaling_stat_distribution == 0u) {
    return nullptr;
  }

  return dbc->scaling_stat_distribution().LookupEntry(item.scaling_stat_distribution);
}

inline const openwow::data::dbc::ScalingStatValuesEntry*
FindScalingStatValuesByLevel(const openwow::data::dbc::DbcLoader* dbc,
                             const std::uint32_t level) {
  if (dbc == nullptr || level == 0u) {
    return nullptr;
  }
  return dbc->scaling_stat_values().LookupEntryByRowIndex(static_cast<int>(level - 1u));
}

template <typename ItemT>
inline std::uint32_t ResolveScalingItemLevel(
    const ItemT& item,
    const openwow::data::dbc::ScalingStatDistributionEntry& distribution,
    std::uint32_t requested_level) {
  std::uint32_t resolved_level = requested_level != 0u ? requested_level : 1u;
  resolved_level = std::max(resolved_level, item.required_level);
  if (distribution.max_level != 0u) {
    resolved_level = std::min(resolved_level, distribution.max_level);
  }
  return std::max(resolved_level, 1u);
}

inline std::uint32_t SelectScalingStatBudget(
    const openwow::data::dbc::ScalingStatValuesEntry& values,
    const std::uint32_t mask) {
  switch (mask & kScalingStatBudgetMask) {
    case 0x1u:
      return values.GetField(2);
    case 0x2u:
      return values.GetField(3);
    case 0x4u:
      return values.GetField(4);
    case 0x8u:
      return values.GetField(17);
    case 0x10u:
      return values.GetField(5);
    case 0x40000u:
      return values.GetField(18);
    default:
      return 0u;
  }
}

inline std::uint32_t SelectScalingArmorValue(
    const openwow::data::dbc::ScalingStatValuesEntry& values,
    const std::uint32_t mask) {
  switch (mask & kScalingArmorMask) {
    case 0x20u:
      return values.GetField(6);
    case 0x40u:
      return values.GetField(7);
    case 0x80u:
      return values.GetField(8);
    case 0x100u:
      return values.GetField(9);
    case 0x80000u:
      return values.GetField(19);
    case 0x100000u:
      return values.GetField(20);
    case 0x200000u:
      return values.GetField(21);
    case 0x400000u:
      return values.GetField(22);
    case 0x800000u:
      return values.GetField(23);
    default:
      return 0u;
  }
}

inline std::optional<ScalingWeaponDpsInfo> SelectScalingWeaponDpsInfo(
    const openwow::data::dbc::ScalingStatValuesEntry& values,
    const std::uint32_t mask) {
  std::uint32_t base_dps = 0u;
  float variance = 0.0f;

  switch (mask & kScalingWeaponMask) {
    case 0x200u:
      base_dps = values.GetField(10);
      variance = 0.30f;
      break;
    case 0x400u:
      base_dps = values.GetField(11);
      variance = 0.20f;
      break;
    case 0x800u:
      base_dps = values.GetField(12);
      variance = 0.30f;
      break;
    case 0x1000u:
      base_dps = values.GetField(13);
      variance = 0.20f;
      break;
    case 0x2000u:
      base_dps = values.GetField(14);
      variance = 0.30f;
      break;
    case 0x4000u:
      base_dps = values.GetField(15);
      variance = 0.30f;
      break;
    default:
      return std::nullopt;
  }

  const float base = static_cast<float>(base_dps);
  return ScalingWeaponDpsInfo{
      .base_dps = base,
      .min_dps = base * (1.0f - variance),
      .max_dps = base * (1.0f + variance),
  };
}

inline std::int32_t ComputeScalingDistributionValue(const std::uint32_t budget,
                                                    const std::uint32_t bonus) {
  return static_cast<std::int32_t>(
      (static_cast<std::uint64_t>(budget) * static_cast<std::uint64_t>(bonus)) / 10000u);
}

inline constexpr std::uint32_t kEquipSlotMainHand = 15u;
inline constexpr std::uint32_t kMainHandSlotBit   = 1u << kEquipSlotMainHand;

inline constexpr std::array<std::uint32_t, 29> kInvTypeSlotMask = {{
    0x00000000u,
    0x00000001u,
    0x00000002u,
    0x00000004u,
    0x00000008u,
    0x00000010u,
    0x00000020u,
    0x00000040u,
    0x00000080u,
    0x00000100u,
    0x00000200u,
    0x00000C00u,
    0x00003000u,
    0x00018000u,
    0x00010000u,
    0x00020000u,
    0x00004000u,
    0x00018000u,
    0x00780000u,
    0x00040000u,
    0x00000010u,
    0x00018000u,
    0x00018000u,
    0x00010000u,
    0x00000000u,
    0x00020000u,
    0x00020000u,
    0x00000000u,
    0x00020000u,
}};

inline bool CanEquipInMainHand(const std::uint32_t inv_type) {
  if (inv_type >= kInvTypeSlotMask.size()) {
    return false;
  }
  return (kInvTypeSlotMask[inv_type] & kMainHandSlotBit) != 0;
}

inline constexpr double kFeralAPDpsThreshold = 54.810001;
inline constexpr double kFeralAPDpsMultiplier = 14.0;
inline constexpr std::uint32_t kItemClassWeapon = 2u;

inline std::int32_t CalculateFeralAPBonusFromWeaponDPS(
    const std::uint32_t item_class,
    const std::uint32_t inv_type,
    const float total_avg_damage,
    const std::uint32_t delay_ms) {
  if (item_class != kItemClassWeapon) {
    return 0;
  }
  if (!CanEquipInMainHand(inv_type)) {
    return 0;
  }
  if (delay_ms == 0u) {
    return 0;
  }
  const double dps = static_cast<double>(total_avg_damage) /
                     (static_cast<double>(static_cast<std::int32_t>(delay_ms)) * 0.001);
  if (dps <= kFeralAPDpsThreshold) {
    return 0;
  }
  return static_cast<std::int32_t>(
      std::floor((dps - kFeralAPDpsThreshold) * kFeralAPDpsMultiplier + 0.5));
}

}

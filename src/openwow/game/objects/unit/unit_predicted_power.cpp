#include "openwow/game/objects/unit/unit_predicted_power.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/unit_defines.h"
#include "openwow/game/update_fields.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace openwow::game {

namespace {

constexpr float kMillisecondsToSeconds = 0.001f;
constexpr std::array<float, 7> kBasePowerRegenRates = {
    0.0f, -12.5f, 5.0f, 10.0f, 0.0f, 0.0f, -12.5f};
constexpr std::array<float, 7> kInterruptedPowerRegenRates = {
    0.0f, 0.0f, 5.0f, 10.0f, 0.0f, 0.0f, 0.0f};

}

void UnitPredictedPowerComponent::EnsureInitialized(CGUnit_C &unit) {
  if (active_) {
    return;
  }
  predicted_powers_.fill(0);
  predicted_powers_[kHealthIndex] = unit.State().GetHealth();
  for (std::uint8_t type = 0; type < 7; ++type) {
    predicted_powers_[type + kIndexBias] = unit.State().GetPower(type);
  }
  active_ = true;
}

PredictedPowerMutationResult UnitPredictedPowerComponent::Set(
    CGUnit_C &unit, const std::int32_t power_type, const std::uint32_t value) {
  PredictedPowerMutationResult result;
  if (power_type < -2 || power_type > 6) {
    return result;
  }

  EnsureInitialized(unit);

  const auto index = static_cast<std::size_t>(power_type + kIndexBias);
  const auto max_value =
      power_type == -2 ? unit.State().GetMaxHealth()
                       : unit.State().GetMaxPower(static_cast<std::uint8_t>(power_type));
  result.value = std::min(value, max_value);
  if (predicted_powers_[index] == result.value) {
    return result;
  }

  predicted_powers_[index] = result.value;
  result.changed = true;
  result.reached_max = (result.value == max_value);
  return result;
}

PredictedPowerMutationResult UnitPredictedPowerComponent::ModifyDisplayedPower(
    CGUnit_C &unit, const std::uint8_t power_type, const std::int32_t delta) {
  PredictedPowerMutationResult result;
  if (power_type > 6 || unit.State().GetPowerType() != power_type) {
    return result;
  }
  if (delta < 0 && (unit.State().GetUnitFlags2() & kDrainSuppressionFlags2) != 0u) {
    return result;
  }

  EnsureInitialized(unit);
  const auto index = static_cast<std::size_t>(power_type + kIndexBias);
  std::int64_t next_value =
      static_cast<std::int64_t>(predicted_powers_[index]) +
      static_cast<std::int64_t>(delta);
  if (next_value < 0) {
    next_value = 0;
  }

  return Set(unit, power_type, static_cast<std::uint32_t>(next_value));
}

PredictedPowerMutationResult UnitPredictedPowerComponent::AdjustHealth(
    CGUnit_C &unit, const std::int32_t delta) {
  if (delta < 0 && (unit.State().GetUnitFlags2() & 0x100u) != 0u) {
    return {};
  }

  EnsureInitialized(unit);
  std::int64_t new_value =
      static_cast<std::int64_t>(predicted_powers_[kHealthIndex]) +
      static_cast<std::int64_t>(delta);

  if (new_value < 1) {
    new_value = 1;
  }

  return Set(unit, -2, static_cast<std::uint32_t>(new_value));
}

PredictedPowerMutationResult UnitPredictedPowerComponent::AdvanceRegen(
    CGUnit_C &unit, const std::int32_t power_type,
    const float regen_rate_per_second, const std::uint32_t elapsed_ms) {
  PredictedPowerMutationResult result;
  if (power_type < 0 || power_type > 6 || elapsed_ms == 0u ||
      std::fabs(regen_rate_per_second) < 0.00000023841858f) {
    return result;
  }

  const double delta =
      static_cast<double>(regen_rate_per_second) *
      static_cast<double>(elapsed_ms) * kMillisecondsToSeconds;

  const std::uint32_t current_value = unit.GetPowerOrHealth(power_type);
  const std::uint32_t max_value = unit.State().GetMaxPower(static_cast<std::uint8_t>(power_type));

  if (delta >= 0.0) {
    if (current_value >= max_value) {
      return result;
    }

    predicted_power_regen_accumulator_millis_ +=
        static_cast<std::int32_t>(std::trunc(delta * 1000.0));
    result = Set(unit, power_type,
                 current_value +
                     static_cast<std::uint32_t>(
                         predicted_power_regen_accumulator_millis_ / 1000));
    predicted_power_regen_accumulator_millis_ %= 1000;
    return result;
  }

  if (current_value == 0u ||
      (unit.State().GetUnitFlags2() & kDrainSuppressionFlags2) != 0u) {
    return result;
  }

  const double next_value =
      static_cast<double>(current_value) +
      static_cast<double>(predicted_power_regen_accumulator_millis_) * 0.001 +
      delta;
  const double clamped_value = std::max(0.0, next_value);
  result = Set(unit, power_type, static_cast<std::uint32_t>(std::trunc(clamped_value)));
  predicted_power_regen_accumulator_millis_ =
      static_cast<std::int32_t>(std::trunc(
          (clamped_value - std::trunc(clamped_value)) * 1000.0));
  return result;
}

void UnitPredictedPowerComponent::SyncFromUpdatedFields(
    const CGUnit_C &unit, const std::vector<std::uint16_t> &updated_fields) {
  if (!active_) {
    return;
  }
  for (const auto field : updated_fields) {
    if (field == UNIT_FIELD_HEALTH) {
      predicted_powers_[kHealthIndex] = unit.GetUInt32(UNIT_FIELD_HEALTH);
      continue;
    }
    if (field >= UNIT_FIELD_POWER1 && field <= UNIT_FIELD_POWER7) {
      const auto power_type =
          static_cast<std::uint8_t>(field - UNIT_FIELD_POWER1);
      predicted_powers_[power_type + kIndexBias] = unit.State().GetPower(power_type);
      continue;
    }
    if (field >= UNIT_FIELD_MAXPOWER1 && field <= UNIT_FIELD_MAXPOWER7) {
      const auto power_type =
          static_cast<std::uint8_t>(field - UNIT_FIELD_MAXPOWER1);
      auto &predicted = predicted_powers_[power_type + kIndexBias];
      predicted = std::min(predicted, unit.State().GetMaxPower(power_type));
      continue;
    }
    if (field == UNIT_FIELD_BYTES_0) {
      const auto display_power_type = unit.State().GetPowerType();
      if (display_power_type <= 6) {
        predicted_powers_[display_power_type + kIndexBias] =
            unit.State().GetPower(display_power_type);
      }
    }
  }
}

std::uint32_t CGUnit_C::GetBasePowerByType(const std::int32_t power_type) const {
  switch (power_type) {
  case -2:
    return GetUInt32(UNIT_FIELD_BASE_HEALTH);
  case 0:
    return GetUInt32(UNIT_FIELD_BASE_MANA);
  case 1:
  case 6:
    return 1000;
  case 2:
  case 3:
    return 100;
  case 5:
    return 1;
  default:
    return 0;
  }
}

std::uint32_t CGUnit_C::GetPowerOrHealth(const std::int32_t power_type) const {
  if (predicted_power_.IsActive() && power_type >= -2 && power_type < 7) {
    return predicted_power_.Get(power_type);
  }
  if (power_type == -2) {
    return State().GetHealth();
  }
  return power_type >= 0 && power_type <= 6
             ? State().GetPower(static_cast<std::uint8_t>(power_type))
             : 0u;
}

UnitPredictedPowerComponent &CGUnit_C::Vitals() noexcept {
  return predicted_power_;
}

const UnitPredictedPowerComponent &CGUnit_C::Vitals() const noexcept {
  return predicted_power_;
}

float CGUnit_C::GetPowerRegenRate(const std::uint8_t power_type) const {
  return power_type <= 6u
             ? kBasePowerRegenRates[power_type] +
                   GetFloat(static_cast<std::uint16_t>(
                       UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER + power_type))
             : 0.0f;
}

float CGUnit_C::GetPowerRegenRateInterrupted(
    const std::uint8_t power_type) const {
  return power_type <= 6u
             ? kInterruptedPowerRegenRates[power_type] +
                   GetFloat(static_cast<std::uint16_t>(
                       UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER +
                       power_type))
             : 0.0f;
}

}

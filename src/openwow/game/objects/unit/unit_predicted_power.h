#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::game {

class CGUnit_C;

struct PredictedPowerMutationResult {
  bool changed{false};
  bool reached_max{false};
  std::uint32_t value{0};
};

class UnitPredictedPowerComponent final {
public:
  UnitPredictedPowerComponent() = default;
  UnitPredictedPowerComponent(const UnitPredictedPowerComponent &) = delete;
  UnitPredictedPowerComponent &operator=(const UnitPredictedPowerComponent &) =
      delete;
  UnitPredictedPowerComponent(UnitPredictedPowerComponent &&) noexcept = default;
  UnitPredictedPowerComponent &operator=(UnitPredictedPowerComponent &&) noexcept =
      default;
  ~UnitPredictedPowerComponent() = default;

  static constexpr std::size_t kIndexBias = 2u;
  static constexpr std::size_t kHealthIndex =
      static_cast<std::size_t>(kIndexBias - 2);
  static constexpr std::uint32_t kDrainSuppressionFlags2 = 0x00000100u;

  [[nodiscard]] bool IsActive() const noexcept { return active_; }

  [[nodiscard]] std::uint32_t Get(std::int32_t power_type) const noexcept {
    if (active_ && power_type >= -2 && power_type < 7) {
      return predicted_powers_[static_cast<std::size_t>(power_type + kIndexBias)];
    }
    return 0;
  }

  void EnsureInitialized(CGUnit_C &unit);

  PredictedPowerMutationResult Set(CGUnit_C &unit, std::int32_t power_type,
                                   std::uint32_t value);

  PredictedPowerMutationResult ModifyDisplayedPower(CGUnit_C &unit,
                                                    std::uint8_t power_type,
                                                    std::int32_t delta);

  PredictedPowerMutationResult AdjustHealth(CGUnit_C &unit, std::int32_t delta);

  PredictedPowerMutationResult AdvanceRegen(CGUnit_C &unit,
                                            std::int32_t power_type,
                                            float regen_rate_per_second,
                                            std::uint32_t elapsed_ms);

  void SyncFromUpdatedFields(const CGUnit_C &unit,
                             const std::vector<std::uint16_t> &updated_fields);

  void NotifyManaRegenInterrupted(std::uint32_t current_time_ms,
                                  std::uint32_t duration_ms = 5000u) noexcept {
    mana_regen_interrupt_until_ms_ = current_time_ms + duration_ms;
  }
  [[nodiscard]] bool HasManaRegenInterrupt(
      std::uint32_t current_time_ms) const noexcept {
    if (mana_regen_interrupt_until_ms_ == 0u) {
      return false;
    }
    return static_cast<std::int32_t>(current_time_ms -
                                     mana_regen_interrupt_until_ms_) < 0;
  }

private:
  bool active_{false};
  std::array<std::uint32_t, 9> predicted_powers_{};
  std::int32_t predicted_power_regen_accumulator_millis_{0};
  std::uint32_t mana_regen_interrupt_until_ms_{0};
};

}

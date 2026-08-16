#pragma once

#include "openwow/game/objects/unit/unit_aura_info.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace openwow::game {

class CGUnit_C;
class WorldSession;

class UnitAuraComponent final {
public:
  static constexpr std::size_t kConditionBitCount = 320;
  static constexpr std::size_t kConditionDwordCount = 10;

  UnitAuraComponent() = default;
  UnitAuraComponent(const UnitAuraComponent &) = delete;
  UnitAuraComponent &operator=(const UnitAuraComponent &) = delete;
  UnitAuraComponent(UnitAuraComponent &&) noexcept = default;
  UnitAuraComponent &operator=(UnitAuraComponent &&) noexcept = default;
  ~UnitAuraComponent() = default;

  void SetAuras(std::vector<AuraInfo> auras) {
    auras_ = std::move(auras);
    cache_dirty_ = true;
  }
  [[nodiscard]] const std::vector<AuraInfo> &All() const noexcept {
    return auras_;
  }
  [[nodiscard]] std::size_t Count() const noexcept { return auras_.size(); }
  [[nodiscard]] const AuraInfo *At(std::size_t index) const noexcept {
    return index < auras_.size() ? &auras_[index] : nullptr;
  }
  [[nodiscard]] ObjectGuid CasterGuidAt(std::size_t index) const noexcept {
    if (index < auras_.size()) {
      return auras_[index].caster_guid;
    }
    return {};
  }
  [[nodiscard]] bool HasSpellId(std::uint32_t spell_id) const noexcept {
    for (const auto &aura : auras_) {
      if (aura.spell_id == spell_id) {
        return true;
      }
    }
    return false;
  }
  [[nodiscard]] const AuraInfo *FindBySpellId(
      std::uint32_t spell_id) const noexcept {
    for (const auto &aura : auras_) {
      if (aura.spell_id == spell_id) {
        return &aura;
      }
    }
    return nullptr;
  }
  void ClearAnimFlags() noexcept {
    for (auto &aura : auras_) {
      aura.flags &= ~0x4000u;
    }
    cache_dirty_ = true;
  }

  void RebuildCache() const;
  [[nodiscard]] std::vector<AuraInfo> Buffs() const;
  [[nodiscard]] std::vector<AuraInfo> Debuffs() const;
  [[nodiscard]] std::size_t NumBuffs() const;
  [[nodiscard]] std::size_t NumDebuffs() const;
  [[nodiscard]] const AuraInfo *BuffAt(std::size_t index) const;
  [[nodiscard]] const AuraInfo *DebuffAt(std::size_t index) const;

  [[nodiscard]] bool TestConditionBit(std::uint32_t bit_index) const noexcept {
    if (bit_index >= kConditionBitCount) {
      return false;
    }
    const auto dword_index = bit_index / 32u;
    const auto bit_in_dword = bit_index % 32u;
    return (condition_bits_[dword_index] & (1u << bit_in_dword)) != 0u;
  }
  void SetConditionBit(std::uint32_t bit_index) noexcept {
    if (bit_index >= kConditionBitCount) {
      return;
    }
    const auto dword_index = bit_index / 32u;
    const auto bit_in_dword = bit_index % 32u;
    condition_bits_[dword_index] |= (1u << bit_in_dword);
  }
  void ClearConditionBits() noexcept { condition_bits_.fill(0); }

  void SetShapeShiftForm(std::uint32_t form_id) noexcept {
    shapeshift_form_ = form_id;
  }
  [[nodiscard]] std::uint32_t ShapeShiftForm() const noexcept {
    return shapeshift_form_;
  }
  void SetStealthDetect(std::uint8_t level) noexcept {
    stealth_detect_ = level;
  }
  [[nodiscard]] std::uint8_t StealthDetect() const noexcept {
    return stealth_detect_;
  }
  void SetInvisibilityDetect(std::uint8_t level) noexcept {
    invisibility_detect_ = level;
  }
  [[nodiscard]] std::uint8_t InvisibilityDetect() const noexcept {
    return invisibility_detect_;
  }

  [[nodiscard]] float QuestExperienceMultiplier(const CGUnit_C &unit) const;

  void ReschedulePeriodicClientAuras(const CGUnit_C &unit,
                                     const WorldSession &session);

private:
  std::vector<AuraInfo> auras_;
  mutable std::vector<AuraInfo> cached_buffs_;
  mutable std::vector<AuraInfo> cached_debuffs_;
  mutable bool cache_dirty_{true};
  std::array<std::uint32_t, kConditionDwordCount> condition_bits_{};
  std::uint32_t shapeshift_form_{0};
  std::uint8_t stealth_detect_{0};
  std::uint8_t invisibility_detect_{0};
};

}

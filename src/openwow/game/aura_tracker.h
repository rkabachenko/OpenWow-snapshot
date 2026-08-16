#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct AuraData {
  std::uint32_t spell_id = 0;
  ObjectGuid caster_guid;
  std::uint32_t duration = 0;
  std::uint32_t expiration = 0;
  std::int32_t charges = 0;
  std::uint32_t stacks = 0;
  std::uint8_t slot = 0;
  std::uint8_t raw_flags = 0;
  bool is_mine = false;
  bool can_apply_aura = false;
  bool is_boss_debuff = false;
  bool has_raw_flags = false;
  bool is_cancellable = false;
  std::uint32_t effect_mask = 0;
  std::int32_t values[3] = {};

  [[nodiscard]] float GetRemainingTime(std::uint32_t current_time) const;
  [[nodiscard]] bool IsExpired(std::uint32_t current_time) const;
  [[nodiscard]] bool IsBuff() const;
  [[nodiscard]] bool IsDebuff() const;
  [[nodiscard]] bool IsPassive() const;
  [[nodiscard]] bool IsHelpfulByOriginalFlags() const;
  [[nodiscard]] bool IsCancelableByOriginalFlags() const;

  [[nodiscard]] std::uint8_t ActiveEffectMask() const {
    constexpr std::uint8_t kEffectBits = 0x07u;
    return has_raw_flags
               ? static_cast<std::uint8_t>(raw_flags & kEffectBits)
               : static_cast<std::uint8_t>(effect_mask & kEffectBits);
  }
};

class AuraTracker {
 public:
  static AuraTracker& Get();

  static constexpr std::uint16_t kMaxBuffSlots    = 56;
  static constexpr std::uint16_t kMaxDebuffSlots   = 64;
  static constexpr std::uint16_t kFirstDebuffSlot  = 56;
  static constexpr std::uint16_t kFirstPassiveSlot = 120;
  static constexpr std::uint16_t kMaxTotalSlots    = 256;

  [[nodiscard]] static bool IsBuffSlot(std::uint8_t slot) {
    return slot < kFirstDebuffSlot;
  }
  [[nodiscard]] static bool IsDebuffSlot(std::uint8_t slot) {
    return slot >= kFirstDebuffSlot && slot < kFirstPassiveSlot;
  }
  [[nodiscard]] static bool IsPassiveSlot(std::uint8_t slot) {
    return slot >= kFirstPassiveSlot;
  }

  void SetAura(const ObjectGuid& unit, std::uint8_t slot,
               const AuraData& aura);
  void RemoveAura(const ObjectGuid& unit, std::uint8_t slot);
  void ClearAuras(const ObjectGuid& unit);

  void SetAllAuras(
      const ObjectGuid& unit,
      const std::vector<std::pair<std::uint8_t, AuraData>>& auras);

  [[nodiscard]] const AuraData* GetAura(const ObjectGuid& unit,
                                        std::uint8_t slot) const;

  [[nodiscard]] std::vector<const AuraData*> GetBuffs(
      const ObjectGuid& unit) const;

  [[nodiscard]] std::vector<const AuraData*> GetDebuffs(
      const ObjectGuid& unit) const;

  [[nodiscard]] std::uint32_t GetBuffCount(const ObjectGuid& unit) const;
  [[nodiscard]] std::uint32_t GetDebuffCount(const ObjectGuid& unit) const;

  [[nodiscard]] const AuraData* FindAuraBySpell(const ObjectGuid& unit,
                                                std::uint32_t spell_id) const;
  [[nodiscard]] bool HasAura(const ObjectGuid& unit,
                             std::uint32_t spell_id) const;

  void ForEachAura(
      const ObjectGuid& unit,
      const std::function<void(std::uint8_t slot, const AuraData&)>& fn)
      const;

  void ForEachAuraAll(
      const ObjectGuid& unit,
      const std::function<void(std::uint8_t slot, const AuraData&)>& fn)
      const;

  void Reset();

 private:
  AuraTracker() = default;

  struct UnitAuras {
    std::array<std::optional<AuraData>, kMaxTotalSlots> slots;
  };

  std::unordered_map<std::uint64_t, UnitAuras> unit_auras_;
  mutable std::mutex mutex_;
};

}

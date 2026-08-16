#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class SpellRangeCategory : std::uint8_t {
  Self      = 0,
  Melee     = 1,
  Ranged    = 2,
  Unlimited = 3,
};

struct SpellRangeEntry {
  std::uint32_t      spellId  = 0;
  float              minRange = 0.0f;
  float              maxRange = 0.0f;
  SpellRangeCategory category = SpellRangeCategory::Self;
};

class SpellRangeDisplay {
 public:
  SpellRangeDisplay() = default;

  void AddSpellRange(const SpellRangeEntry& entry);

  [[nodiscard]] std::optional<SpellRangeEntry> GetSpellRange(
      std::uint32_t spellId) const;

  [[nodiscard]] SpellRangeCategory GetCategory(std::uint32_t spellId) const;

  [[nodiscard]] bool IsInRange(std::uint32_t spellId, float distance) const;

  [[nodiscard]] float GetMinRange(std::uint32_t spellId) const;
  [[nodiscard]] float GetMaxRange(std::uint32_t spellId) const;

  void  SetDefaultMeleeRange(float v);
  [[nodiscard]] float GetDefaultMeleeRange() const;

  void  SetDefaultRangedRange(float v);
  [[nodiscard]] float GetDefaultRangedRange() const;

  [[nodiscard]] bool IsRangedSpell(std::uint32_t spellId) const;
  [[nodiscard]] bool IsMeleeSpell(std::uint32_t spellId) const;
  [[nodiscard]] bool IsSelfSpell(std::uint32_t spellId) const;

  [[nodiscard]] static std::uint32_t GetColorForRange(float normalizedDistance);

  [[nodiscard]] static std::uint32_t GetInterpolatedColorForRange(float normalizedDistance);

  void ApplyRangeModifier(std::uint32_t spellId, float modifier);

  [[nodiscard]] float GetNormalizedDistance(std::uint32_t spellId, float distance) const;

  [[nodiscard]] std::size_t GetRangeEntryCount() const;

  [[nodiscard]] bool HasSpellRange(std::uint32_t spellId) const;

  void RemoveSpellRange(std::uint32_t spellId);

  [[nodiscard]] std::vector<std::uint32_t> GetAllSpellIds() const;

  [[nodiscard]] float GetDefaultRangeForCategory(SpellRangeCategory category) const;

  void Clear();

 private:
  std::unordered_map<std::uint32_t, SpellRangeEntry> ranges_;
  float default_melee_range_  = 5.0f;
  float default_ranged_range_ = 30.0f;
};

}

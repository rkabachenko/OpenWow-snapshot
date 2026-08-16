
#include "openwow/game/spell_range.h"

namespace openwow::game {

void SpellRangeDisplay::AddSpellRange(const SpellRangeEntry& entry) {
  ranges_[entry.spellId] = entry;
}

std::optional<SpellRangeEntry> SpellRangeDisplay::GetSpellRange(
    std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  if (it != ranges_.end()) return it->second;
  return std::nullopt;
}

SpellRangeCategory SpellRangeDisplay::GetCategory(
    std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  if (it != ranges_.end()) return it->second.category;
  return SpellRangeCategory::Self;
}

bool SpellRangeDisplay::IsInRange(std::uint32_t spellId,
                                  float distance) const {
  auto it = ranges_.find(spellId);
  if (it == ranges_.end()) return false;
  const auto& e = it->second;
  return distance >= e.minRange && distance <= e.maxRange;
}

float SpellRangeDisplay::GetMinRange(std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  if (it != ranges_.end()) return it->second.minRange;
  return 0.0f;
}

float SpellRangeDisplay::GetMaxRange(std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  if (it != ranges_.end()) return it->second.maxRange;
  return 0.0f;
}

void SpellRangeDisplay::SetDefaultMeleeRange(float v)  { default_melee_range_ = v; }
float SpellRangeDisplay::GetDefaultMeleeRange() const   { return default_melee_range_; }

void SpellRangeDisplay::SetDefaultRangedRange(float v) { default_ranged_range_ = v; }
float SpellRangeDisplay::GetDefaultRangedRange() const  { return default_ranged_range_; }

bool SpellRangeDisplay::IsRangedSpell(std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  return it != ranges_.end() && it->second.category == SpellRangeCategory::Ranged;
}

bool SpellRangeDisplay::IsMeleeSpell(std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  return it != ranges_.end() && it->second.category == SpellRangeCategory::Melee;
}

bool SpellRangeDisplay::IsSelfSpell(std::uint32_t spellId) const {
  auto it = ranges_.find(spellId);
  return it != ranges_.end() && it->second.category == SpellRangeCategory::Self;
}

std::uint32_t SpellRangeDisplay::GetColorForRange(float normalizedDistance) {
  if (normalizedDistance <= 0.75f) {

    return 0xFF00FF00;
  } else if (normalizedDistance <= 1.0f) {

    return 0xFFFFFF00;
  } else {

    return 0xFFFF0000;
  }
}

void SpellRangeDisplay::Clear() {
  ranges_.clear();
}

void SpellRangeDisplay::ApplyRangeModifier(std::uint32_t spellId,
                                           float modifier) {
  auto it = ranges_.find(spellId);
  if (it == ranges_.end()) return;
  auto& entry = it->second;

  entry.maxRange *= modifier;

  if (entry.minRange > 0.0f) {
    entry.minRange *= modifier;
  }

  constexpr float kMaxReasonableRange = 200.0f;
  if (entry.maxRange > kMaxReasonableRange) {
    entry.maxRange = kMaxReasonableRange;
  }
  if (entry.minRange < 0.0f) {
    entry.minRange = 0.0f;
  }

  if (entry.minRange > entry.maxRange) {
    entry.minRange = entry.maxRange;
  }
}

float SpellRangeDisplay::GetNormalizedDistance(std::uint32_t spellId,
                                               float distance) const {
  auto it = ranges_.find(spellId);
  if (it == ranges_.end()) return 1.0f;
  const auto& e = it->second;

  if (e.category == SpellRangeCategory::Self ||
      e.category == SpellRangeCategory::Unlimited) {
    return 0.0f;
  }

  if (e.maxRange <= 0.0f) return 0.0f;

  if (distance <= e.minRange) return 0.0f;

  float effective = distance - e.minRange;
  float effectiveMax = e.maxRange - e.minRange;
  if (effectiveMax <= 0.0f) return 0.0f;
  return effective / effectiveMax;
}

std::size_t SpellRangeDisplay::GetRangeEntryCount() const {
  return ranges_.size();
}

bool SpellRangeDisplay::HasSpellRange(std::uint32_t spellId) const {
  return ranges_.find(spellId) != ranges_.end();
}

void SpellRangeDisplay::RemoveSpellRange(std::uint32_t spellId) {
  ranges_.erase(spellId);
}

std::vector<std::uint32_t> SpellRangeDisplay::GetAllSpellIds() const {
  std::vector<std::uint32_t> ids;
  ids.reserve(ranges_.size());
  for (const auto& [id, _] : ranges_) {
    ids.push_back(id);
  }
  return ids;
}

std::uint32_t SpellRangeDisplay::GetInterpolatedColorForRange(
    float normalizedDistance) {

  if (normalizedDistance <= 0.60f) {
    return 0xFF00FF00;
  }
  if (normalizedDistance <= 0.85f) {

    float t = (normalizedDistance - 0.60f) / 0.25f;
    auto r = static_cast<std::uint8_t>(255.0f * t);
    return 0xFF000000 | (static_cast<std::uint32_t>(r) << 16) | 0x0000FF00;
  }
  if (normalizedDistance <= 1.0f) {

    float t = (normalizedDistance - 0.85f) / 0.15f;
    auto g = static_cast<std::uint8_t>(255.0f * (1.0f - t));
    return 0xFFFF0000 | (static_cast<std::uint32_t>(g) << 8);
  }
  return 0xFFFF0000;
}

float SpellRangeDisplay::GetDefaultRangeForCategory(
    SpellRangeCategory category) const {
  switch (category) {
    case SpellRangeCategory::Self:      return 0.0f;
    case SpellRangeCategory::Melee:     return default_melee_range_;
    case SpellRangeCategory::Ranged:    return default_ranged_range_;
    case SpellRangeCategory::Unlimited: return 50000.0f;
    default:                            return 0.0f;
  }
}

}

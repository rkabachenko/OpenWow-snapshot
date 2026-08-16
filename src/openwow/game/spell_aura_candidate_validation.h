#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace openwow::data::dbc {
struct SpellEntry;
}

namespace openwow::game {

struct AuraData;
class CGUnit_C;
class WorldSession;
enum class SpellCastResult : std::uint8_t;

struct SpellAuraCandidateData {
  std::uint32_t mechanic = 0;
  std::uint32_t attributes = 0;
  std::uint32_t attributes_ex = 0;
  std::uint32_t attributes_ex4 = 0;
  std::uint32_t dispel_type = 0;
  std::array<std::uint32_t, 3> effect_ids{};
  std::array<std::uint32_t, 3> effect_mechanics{};
};

enum class SpellAuraCandidateMatch : std::uint8_t {
  kDispelType,
  kMechanic,
};

struct SpellAuraCandidateCriteria {
  SpellAuraCandidateMatch match = SpellAuraCandidateMatch::kDispelType;
  std::uint32_t requested_mask_or_mechanic = 0;
  bool helpful_only = false;
  bool require_applied_effect = false;
  bool reject_nonstealable = false;
};

struct SpellTargetCandidateUnit {
  const CGUnit_C* unit = nullptr;
  bool is_implicit_caster = false;
};

[[nodiscard]] SpellAuraCandidateData BuildSpellAuraCandidateData(
    const data::dbc::SpellEntry& spell);

[[nodiscard]] bool IsAuraVisibilityOnlyEffect(std::uint32_t effect_id);

[[nodiscard]] bool IsSpellAuraCandidate(
    const SpellAuraCandidateData& aura_spell,
    const AuraData& aura,
    const SpellAuraCandidateCriteria& criteria);

[[nodiscard]] SpellCastResult ValidateSpellTargetCandidates(
    const WorldSession& session,
    const data::dbc::SpellEntry& spell,
    const CGUnit_C& caster,
    std::span<const SpellTargetCandidateUnit> candidate_units);

}

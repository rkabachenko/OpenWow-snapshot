#include "openwow/game/inventory/items/item_use_requirements.h"

#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/pvp_info.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spellbook_system.h"
#include <limits>

namespace openwow::game {
namespace {

constexpr std::uint32_t kSkillRequirementBypassFlags = 0x20040000u;

std::uint32_t BuildRetailIdentityMask(const std::uint8_t identity) {
  return std::uint32_t{1}
         << ((static_cast<std::uint32_t>(identity) - 1u) & 31u);
}

}

ItemUseRequirementSources::ItemUseRequirementSources(
    const SpellbookSystem& spellbook, const PvPInfo& pvp,
    const ReputationInfo& reputation) noexcept
    : spellbook_(&spellbook), pvp_(&pvp), reputation_(&reputation) {}

bool ItemUseRequirementSources::KnowsSpellOrSupersedingRank(
    const std::uint32_t spell, const std::uint8_t race,
    const std::uint8_t player_class) const {
  return spellbook_ != nullptr &&
         spellbook_->HasSpellOrSupersedingRank(spell, race, player_class);
}

std::uint32_t ItemUseRequirementSources::HighestHonorRank() const {
  return pvp_ != nullptr ? pvp_->GetHighestRank() : 0;
}

std::int32_t ItemUseRequirementSources::ReputationStanding(
    const std::uint32_t faction) const {
  return reputation_ != nullptr
             ? reputation_->GetCurrentStanding(
                   static_cast<std::int32_t>(faction))
             : 0;
}

ItemUseRequirementView BuildItemUseRequirementView(
    const ItemTemplate& item_template) {
  return ItemUseRequirementView{
      .item_class = static_cast<std::uint32_t>(item_template.item_class),
      .sub_class = item_template.subclass,
      .flags = item_template.flags,
      .allowable_class = item_template.allowable_class,
      .allowable_race = item_template.allowable_race,
      .required_level = item_template.required_level,
      .required_skill = item_template.required_skill,
      .required_skill_rank = item_template.required_skill_rank,
      .required_spell = item_template.required_spell,
      .required_honor_rank = item_template.required_honor_rank,
      .required_city_rank = item_template.required_city_rank,
      .required_rep_faction = item_template.required_reputation_faction,
      .required_rep_rank = item_template.required_reputation_rank,
  };
}

bool PlayerMeetsItemUseRequirements(
    const CGPlayer_C& player, const ItemUseRequirementView& item_template,
    const ItemUseRequirementSources& sources,
    const std::uint32_t proficiency_mask) {
  if (item_template.required_level > player.State().GetLevel()) {
    return false;
  }

  const auto class_mask = BuildRetailIdentityMask(player.State().GetClass());
  const auto race_mask = BuildRetailIdentityMask(player.State().GetRace());
  if ((static_cast<std::uint32_t>(item_template.allowable_class) &
       class_mask) == 0u ||
      (static_cast<std::uint32_t>(item_template.allowable_race) &
       race_mask) == 0u) {
    return false;
  }

  const auto item_class = static_cast<std::uint8_t>(item_template.item_class);
  if (proficiency_mask != 0 && item_class < 17u) {
    const auto subclass_mask =
        std::uint32_t{1} << (item_template.sub_class & 31u);
    if ((proficiency_mask & subclass_mask) == 0u) {
      return false;
    }
  }

  if (item_template.required_skill != 0u &&
      (item_template.flags & kSkillRequirementBypassFlags) == 0u) {
    if (item_template.required_skill >
        std::numeric_limits<std::uint16_t>::max()) {
      return false;
    }
    const auto skill = player.FindActiveSkillValues(
        static_cast<std::uint16_t>(item_template.required_skill));
    if (!skill.has_value() ||
        skill->adjusted_value < item_template.required_skill_rank) {
      return false;
    }
  }

  if (item_template.required_spell != 0u &&
      !sources.KnowsSpellOrSupersedingRank(
          item_template.required_spell, player.State().GetRace(),
          player.State().GetClass())) {
    return false;
  }

  if (item_template.required_honor_rank != 0u &&
      sources.HighestHonorRank() < item_template.required_honor_rank) {
    return false;
  }

  if (item_template.required_city_rank != 0u &&
      !player.HasTitle((item_template.required_city_rank - 1u) & 31u)) {
    return false;
  }

  if (item_template.required_rep_faction != 0u) {
    if (item_template.required_rep_rank >=
        kStandingMin.size()) {
      return false;
    }
    if (sources.ReputationStanding(item_template.required_rep_faction) <
            kStandingMin[item_template.required_rep_rank]) {
      return false;
    }
  }

  return true;
}

}

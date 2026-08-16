#pragma once

#include <cstdint>
namespace openwow::game {

class CGPlayer_C;
class PvPInfo;
class ReputationInfo;
class SpellbookSystem;
struct ItemTemplate;

class ItemUseRequirementSources {
 public:
  ItemUseRequirementSources() = default;
  ItemUseRequirementSources(
      const SpellbookSystem&, const PvPInfo&,
      const ReputationInfo&) noexcept;
  [[nodiscard]] bool KnowsSpellOrSupersedingRank(
      std::uint32_t spell, std::uint8_t race,
      std::uint8_t player_class) const;
  [[nodiscard]] std::uint32_t HighestHonorRank() const;
  [[nodiscard]] std::int32_t ReputationStanding(
      std::uint32_t faction) const;

 private:
  const SpellbookSystem* spellbook_ = nullptr;
  const PvPInfo* pvp_ = nullptr;
  const ReputationInfo* reputation_ = nullptr;
};

struct ItemUseRequirementView {
  std::uint32_t item_class = 0;
  std::uint32_t sub_class = 0;
  std::uint32_t flags = 0;
  std::int32_t allowable_class = -1;
  std::int32_t allowable_race = -1;
  std::uint32_t required_level = 0;
  std::uint32_t required_skill = 0;
  std::uint32_t required_skill_rank = 0;
  std::uint32_t required_spell = 0;
  std::uint32_t required_honor_rank = 0;
  std::uint32_t required_city_rank = 0;
  std::uint32_t required_rep_faction = 0;
  std::uint32_t required_rep_rank = 0;
};

[[nodiscard]] ItemUseRequirementView BuildItemUseRequirementView(
    const ItemTemplate& item_template);

[[nodiscard]] bool PlayerMeetsItemUseRequirements(
    const CGPlayer_C& player, const ItemUseRequirementView& item_template,
    const ItemUseRequirementSources& sources,
    std::uint32_t proficiency_mask = 0);

}

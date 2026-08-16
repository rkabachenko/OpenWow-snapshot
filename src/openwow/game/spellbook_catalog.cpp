#include "openwow/game/spellbook_catalog.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttr0TradeSpell = 0x20u;
constexpr std::uint32_t kSpellEffectSummon = 28u;
constexpr std::uint32_t kSpellAuraMounted = 78u;

constexpr std::uint32_t kSummonPropertiesCompanionSlot = 5u;

}

LearnedSpellCatalog ClassifyLearnedSpell(
    const openwow::data::dbc::SpellEntry& spell,
    const openwow::data::dbc::DbcLoader& dbc_loader) {
  if ((spell.attributes & kSpellAttr0HiddenClientside) != 0u) {
    return LearnedSpellCatalog::HiddenClientside;
  }
  if ((spell.attributes & kSpellAttr0TradeSpell) != 0u) {
    return LearnedSpellCatalog::TradeSkill;
  }

  const bool has_primary_summon_effect =
      spell.effect[0] == kSpellEffectSummon;
  bool summons_companion = false;
  if (has_primary_summon_effect) {
    const auto summon_properties_id =
        static_cast<std::uint32_t>(spell.effect_misc_value_b[0]);
    const auto* summon_properties =
        dbc_loader.summon_properties().LookupEntry(summon_properties_id);

    summons_companion =
        summon_properties != nullptr &&
        summon_properties->slot == kSummonPropertiesCompanionSlot;
  }
  const bool applies_mounted_aura =
      spell.effect_apply_aura[0] == kSpellAuraMounted;
  if (summons_companion || applies_mounted_aura) {

    return has_primary_summon_effect ? LearnedSpellCatalog::Critter
                                     : LearnedSpellCatalog::Mount;
  }
  if ((spell.attributes_ex4 & kSpellAttrEx4HiddenSpellbook) != 0u) {
    return LearnedSpellCatalog::HiddenSpellbook;
  }
  return LearnedSpellCatalog::Spellbook;
}

bool IsVisibleSpellbookCatalog(const LearnedSpellCatalog catalog) {
  return catalog == LearnedSpellCatalog::Spellbook;
}

bool IsSpellNameLookupCatalog(const LearnedSpellCatalog catalog) {
  return catalog != LearnedSpellCatalog::TradeSkill &&
         catalog != LearnedSpellCatalog::HiddenClientside;
}

}

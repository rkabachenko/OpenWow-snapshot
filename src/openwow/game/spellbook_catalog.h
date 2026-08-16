#pragma once

#include <cstdint>

namespace openwow::data::dbc {
class DbcLoader;
struct SpellEntry;
}

namespace openwow::game {

inline constexpr std::uint32_t kSpellAttr0HiddenClientside = 0x80u;

inline constexpr std::uint32_t kSpellAttrEx4HiddenSpellbook = 0x8000u;

enum class LearnedSpellCatalog {
  Spellbook,
  TradeSkill,
  Critter,
  Mount,
  HiddenClientside,
  HiddenSpellbook,
};

[[nodiscard]] LearnedSpellCatalog ClassifyLearnedSpell(
    const openwow::data::dbc::SpellEntry& spell,
    const openwow::data::dbc::DbcLoader& dbc_loader);

[[nodiscard]] bool IsVisibleSpellbookCatalog(LearnedSpellCatalog catalog);
[[nodiscard]] bool IsSpellNameLookupCatalog(LearnedSpellCatalog catalog);

}

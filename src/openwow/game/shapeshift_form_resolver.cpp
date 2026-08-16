#include "openwow/game/shapeshift_form_resolver.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/attack_action_shapeshift.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <bit>
#include <iterator>

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttrEx2HiddenFromStanceBar = 0x00000002u;
constexpr std::uint32_t kSpellAttrEx2AlwaysOnStanceBar = 0x00000010u;

struct ShapeshiftFormSpell {
  std::uint32_t spell_id = 0;
  std::int32_t stance_bar_order = -1;
};

bool QueryHasShapeshiftAura(const SpellQueryResult &query) {
  return std::find(query.effectApplyAura.begin(), query.effectApplyAura.end(),
                   kShapeshiftAuraType) != query.effectApplyAura.end();
}

bool SpellEntryHasShapeshiftAura(const data::dbc::SpellEntry &spell) {
  return std::find(spell.effect_apply_aura.begin(), spell.effect_apply_aura.end(),
                   kShapeshiftAuraType) != spell.effect_apply_aura.end();
}

bool IsShapeshiftBarSpell(const std::uint32_t attributes_ex2,
                          const bool has_shapeshift_aura) {
  return (attributes_ex2 & kSpellAttrEx2HiddenFromStanceBar) == 0 &&
         ((attributes_ex2 & kSpellAttrEx2AlwaysOnStanceBar) != 0 ||
          has_shapeshift_aura);
}

bool RetailShapeshiftFormLess(const ShapeshiftFormSpell &lhs,
                              const ShapeshiftFormSpell &rhs) {

  if (lhs.stance_bar_order < 0 && rhs.stance_bar_order < 0) {
    return lhs.spell_id < rhs.spell_id;
  }
  if (lhs.stance_bar_order == -1) {
    return false;
  }
  if (rhs.stance_bar_order == -1) {
    return true;
  }
  return lhs.stance_bar_order < rhs.stance_bar_order;
}

}

bool IsShapeshiftFormSpell(const data::dbc::SpellEntry &spell) {
  return IsShapeshiftBarSpell(spell.attributes_ex2,
                              SpellEntryHasShapeshiftAura(spell));
}

std::vector<std::uint32_t> ResolveShapeshiftFormSpellIds(const WorldSession *session) {
  if (session == nullptr) {
    return {};
  }

  const auto *dbc = session->GetDbcLoader();
  const auto &spells = session->spell_book().spells();

  std::vector<ShapeshiftFormSpell> form_spells;
  form_spells.reserve(spells.size());

  for (const auto spell_id : spells) {
    if (spell_id == 0) {
      continue;
    }

    const auto *spell = dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
    if (spell != nullptr) {
      if (IsShapeshiftFormSpell(*spell)) {
        form_spells.push_back({
            .spell_id = spell_id,
            .stance_bar_order = std::bit_cast<std::int32_t>(spell->stance_bar_order),
        });
      }
      continue;
    }

    const auto query = SpellQueryBridge::Get().Query(spell_id);
    if (query.has_value() &&
        IsShapeshiftBarSpell(query->attributesEx2, QueryHasShapeshiftAura(*query))) {
      form_spells.push_back({.spell_id = spell_id, .stance_bar_order = -1});
    }
  }

  std::stable_sort(form_spells.begin(), form_spells.end(), RetailShapeshiftFormLess);
  form_spells.erase(
      std::unique(form_spells.begin(), form_spells.end(),
                  [](const ShapeshiftFormSpell &lhs, const ShapeshiftFormSpell &rhs) {
                    return lhs.spell_id == rhs.spell_id;
                  }),
      form_spells.end());

  std::vector<std::uint32_t> result;
  result.reserve(form_spells.size());
  std::transform(form_spells.begin(), form_spells.end(), std::back_inserter(result),
                 [](const ShapeshiftFormSpell &spell) { return spell.spell_id; });
  return result;
}

std::uint32_t ResolveShapeshiftFormIdFromSpell(const WorldSession &session,
                                               const std::uint32_t spell_id) {
  const auto query = SpellQueryBridge::Get().Query(spell_id);
  if (query.has_value()) {
    for (std::size_t i = 0; i < query->effectApplyAura.size(); ++i) {
      if (query->effectApplyAura[i] == kShapeshiftAuraType) {
        return static_cast<std::uint32_t>(query->effectMiscValue[i]);
      }
    }
  }

  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  for (std::size_t i = 0; i < spell->effect_apply_aura.size(); ++i) {
    if (spell->effect_apply_aura[i] == kShapeshiftAuraType) {
      return static_cast<std::uint32_t>(spell->effect_misc_value[i]);
    }
  }

  return 0;
}

}

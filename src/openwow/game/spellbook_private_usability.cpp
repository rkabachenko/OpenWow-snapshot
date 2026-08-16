#include "openwow/game/spellbook_private_usability.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cast_execution.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_effective_variant.h"
#include "openwow/game/spell_learning_reference.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::uint32_t kSpellAttrEx4PrivateUsability = 0x200u;

const data::dbc::SpellEntry* LookupSpell(const WorldSession& session,
                                         const std::uint32_t spell_id) {
  const auto* const dbc = session.GetDbcLoader();
  return dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
}

std::string_view SpellName(const data::dbc::SpellEntry& spell) {
  return spell.spell_name;
}

bool PrivateCollectionNamesEqual(const std::string_view lhs,
                                 const std::string_view rhs) {
  const std::string lhs_string(lhs);
  const std::string rhs_string(rhs);
  return core::SStrCmpI(lhs_string.c_str(), rhs_string.c_str(),
                        0x7FFFFFFFu) == 0;
}

std::uint32_t SpellRank(const data::dbc::SpellEntry& spell) {
  return ResolveSpellProgressionRank(spell.rank, spell.spell_level);
}

bool IsPrivateFirstSpell(const data::dbc::SpellEntry& spell) {
  return (spell.attributes_ex4 & kSpellAttrEx4PrivateUsability) != 0u;
}

bool IsActive(const SpellbookPrivateUsability::Entry& entry) {
  return entry.broad_usable && !entry.power_unavailable;
}

bool ResolveBroadUsability(WorldSession& session,
                           const CGPlayer_C& player,
                           const data::dbc::SpellEntry& spell) {

  ItemTemplate optional_spell_flags{};
  const auto* optional_item = static_cast<const ItemTemplate*>(nullptr);
  if ((spell.attributes_ex2 & 0x10000000u) != 0u) {
    optional_spell_flags.flags = 0x10000000u;
    optional_item = &optional_spell_flags;
  }
  const auto result = ValidateSpellRequirementsDetailed(
      session, reinterpret_cast<std::uintptr_t>(&player),
      reinterpret_cast<std::uintptr_t>(&spell), 0u,
      reinterpret_cast<std::uintptr_t>(optional_item), false);
  return result.IsSuccess();
}

void RefreshEntry(WorldSession& session, const CGPlayer_C& player,
                  SpellbookPrivateUsability::Entry& entry) {
  const auto* const spell = LookupSpell(session, entry.spell_id);
  if (spell == nullptr) {
    entry.broad_usable = false;
    entry.power_unavailable = true;
    return;
  }

  entry.broad_usable = ResolveBroadUsability(session, player, *spell);
  entry.power_unavailable = !HasEnoughSpellPower(*spell, player, session);
}

void QueueShapeshiftFormsChanged() {
  ui::game::ScriptEventDispatch::Get().QueueGlobalEvent(
      ui::game::events::UPDATE_SHAPESHIFT_FORMS);
}

void SortStanceCollection(WorldSession& session,
                          std::vector<SpellbookPrivateUsability::Entry>& entries) {
  const auto ordered_ids = ResolveShapeshiftFormSpellIds(&session);
  std::vector<std::uint32_t> order;
  order.reserve(ordered_ids.size());
  for (const auto spell_id : ordered_ids) {
    order.push_back(spell_id);
  }

  const auto rank = [&order](const std::uint32_t spell_id) {
    const auto it = std::find(order.begin(), order.end(), spell_id);
    return it == order.end()
               ? order.size()
               : static_cast<std::size_t>(it - order.begin());
  };
  std::stable_sort(entries.begin(), entries.end(),
                   [&rank](const auto& lhs, const auto& rhs) {
                     return rank(lhs.spell_id) < rank(rhs.spell_id);
                   });
}

}

void SpellbookPrivateUsability::Reset() noexcept {
  first_collection_.clear();
  stance_collection_.clear();
}

void SpellbookPrivateUsability::OnSpellLearned(
    WorldSession& session, const std::uint32_t spell_id,
    const std::uint32_t superseded_spell_id) {
  if (spell_id == 0u) {
    return;
  }

  const auto* const spell = LookupSpell(session, spell_id);
  if (spell == nullptr) {
    return;
  }

  if (IsPrivateFirstSpell(*spell)) {
    const auto existing = std::find_if(
        first_collection_.begin(), first_collection_.end(),
        [&](const Entry& entry) {
          const auto* const existing_spell = LookupSpell(session, entry.spell_id);
          return existing_spell != nullptr &&
                 PrivateCollectionNamesEqual(SpellName(*existing_spell),
                                             SpellName(*spell));
        });

    if (existing == first_collection_.end()) {
      first_collection_.push_back(Entry{.spell_id = spell_id});
    } else {
      const auto* const existing_spell = LookupSpell(session, existing->spell_id);
      if (existing_spell == nullptr || SpellRank(*spell) > SpellRank(*existing_spell)) {
        existing->spell_id = spell_id;
      }
    }
  }

  if (!IsShapeshiftFormSpell(*spell)) {
    return;
  }

  if (superseded_spell_id != 0u && superseded_spell_id != spell_id) {
    const auto existing = std::find_if(
        stance_collection_.begin(), stance_collection_.end(),
        [superseded_spell_id](const Entry& entry) {
          return entry.spell_id == superseded_spell_id;
        });
    if (existing == stance_collection_.end()) {
      return;
    }
    existing->spell_id = spell_id;
    const auto* const player = session.objects().GetLocalPlayerTyped();
    if (player != nullptr) {
      RefreshEntry(session, *player, *existing);
    }
    SortStanceCollection(session, stance_collection_);
    QueueShapeshiftFormsChanged();
    return;
  }

  const auto already_present = std::find_if(
      stance_collection_.begin(), stance_collection_.end(),
      [spell_id](const Entry& entry) { return entry.spell_id == spell_id; });
  if (already_present != stance_collection_.end()) {
    return;
  }

  stance_collection_.push_back(Entry{.spell_id = spell_id});
  const auto* const player = session.objects().GetLocalPlayerTyped();
  if (player != nullptr) {
    RefreshEntry(session, *player, stance_collection_.back());
  }
  SortStanceCollection(session, stance_collection_);
  QueueShapeshiftFormsChanged();
}

void SpellbookPrivateUsability::OnSpellForgotten(
    WorldSession& , const std::uint32_t spell_id) {
  if (spell_id == 0u) {
    return;
  }

  const auto old_size = stance_collection_.size();
  stance_collection_.erase(
      std::remove_if(stance_collection_.begin(), stance_collection_.end(),
                     [spell_id](const Entry& entry) {
                       return entry.spell_id == spell_id;
                     }),
      stance_collection_.end());
  if (stance_collection_.size() != old_size) {
    QueueShapeshiftFormsChanged();
  }
}

void SpellbookPrivateUsability::Refresh(WorldSession& session) {
  const auto* const player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (auto& entry : first_collection_) {
    const bool was_active = IsActive(entry);
    RefreshEntry(session, *player, entry);
    if (!was_active && IsActive(entry)) {
      const auto* const spell = LookupSpell(session, entry.spell_id);
      if (spell != nullptr) {
        const std::string name(SpellName(*spell));
        CombatLog_FireCombatTextSS(CombatTextMsgIdx::kSpellActive,
                                   name.c_str());
      }
    }
  }

  bool stance_changed = false;
  for (auto& entry : stance_collection_) {
    const bool was_active = IsActive(entry);
    RefreshEntry(session, *player, entry);
    stance_changed |= was_active != IsActive(entry);
  }

  if (stance_changed) {
    dispatch.FireEvent(ui::game::events::UPDATE_SHAPESHIFT_USABLE);
  }
  dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
}

void SpellbookPrivateUsability::RefreshPower(WorldSession& session) {
  const auto* const player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return;
  }

  auto& dispatch = ui::game::ScriptEventDispatch::Get();
  for (auto& entry : first_collection_) {
    if (!entry.broad_usable) {
      continue;
    }

    const bool was_active = IsActive(entry);
    const auto* const spell = LookupSpell(session, entry.spell_id);
    if (spell == nullptr) {
      continue;
    }
    entry.power_unavailable = !HasEnoughSpellPower(*spell, *player, session);
    if (!was_active && IsActive(entry)) {
      const std::string name(SpellName(*spell));
      CombatLog_FireCombatTextSS(CombatTextMsgIdx::kSpellActive,
                                 name.c_str());
    }
  }

  bool stance_changed = false;
  for (auto& entry : stance_collection_) {
    if (!entry.broad_usable) {
      continue;
    }

    const bool was_active = IsActive(entry);
    const auto* const spell = LookupSpell(session, entry.spell_id);
    if (spell == nullptr) {
      continue;
    }
    entry.power_unavailable = !HasEnoughSpellPower(*spell, *player, session);
    stance_changed |= was_active != IsActive(entry);
  }

  if (stance_changed) {
    dispatch.FireEvent(ui::game::events::UPDATE_SHAPESHIFT_USABLE);
  }
  dispatch.FireEvent(ui::game::events::SPELL_UPDATE_USABLE);
}

}

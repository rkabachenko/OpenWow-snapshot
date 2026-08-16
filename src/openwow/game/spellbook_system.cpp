
#include "openwow/game/spellbook_system.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/game_settings.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/spell_learning_reference.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/query_cache.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::size_t kUnboundedStormStringCompare = 0x7FFFFFFFu;
constexpr std::uint32_t kReferAFriendSummonSpellFamilyName = 0x98;
constexpr std::uint32_t kSpellAttrEx4AutoRangedCombat = 0x01000000u;

constexpr std::uint32_t kHiddenSkillLineFlag = 0x00000080u;
constexpr std::uint32_t kSkinnableGatherInteractionEffectId = 95u;
constexpr std::uint32_t kCorpseOrPlayerGatherInteractionEffectId = 116u;
constexpr std::string_view kGeneralSpellTabName = "General";
constexpr std::string_view kGeneralSpellTabTexture =
    "Interface\\Icons\\Ability_Kick";

struct GatherInteractionPrimaryEffect {
  std::uint32_t effect_id = 0;
  std::int32_t misc_value = 0;
};

struct SpellSortMetadata {
  std::string name;
  std::string subtext;
  std::uint32_t fallback_rank = 0;
};

struct CompanionSortRecord {
  std::uint32_t spell_id = 0;
  std::optional<std::string> creature_name;
};

std::optional<std::string> ResolveCompanionCreatureName(
    const openwow::data::dbc::DbcLoader& dbc_loader,
    const QueryCache& query_cache, const std::uint32_t spell_id) {
  const auto* spell = dbc_loader.spell().LookupEntry(spell_id);
  if (spell == nullptr || spell->effect_misc_value[0] <= 0) {
    return std::nullopt;
  }

  const auto* creature = query_cache.GetCreatureTemplate(
      static_cast<std::uint32_t>(spell->effect_misc_value[0]));
  if (creature == nullptr) {
    return std::nullopt;
  }
  return creature->name;
}

std::optional<std::string_view> ResolveSpellTabSortName(
    const openwow::data::dbc::DbcLoader* dbc_loader,
    const SpellTab& tab) {
  if (tab.skill_line_id != 0 && dbc_loader != nullptr) {
    if (const auto* skill_line = dbc_loader->skill_line().LookupEntry(tab.skill_line_id);
        skill_line != nullptr && !skill_line->name.empty()) {
      return skill_line->name;
    }
    return std::nullopt;
  }

  if (!tab.name.empty()) {
    return std::string_view(tab.name);
  }

  return std::nullopt;
}

void SortSpellTabsForDisplay(std::vector<SpellTab>* tabs,
                             const openwow::data::dbc::DbcLoader* dbc_loader) {
  if (tabs == nullptr || tabs->size() < 2) {
    return;
  }

  std::stable_sort(
      tabs->begin(), tabs->end(),
      [dbc_loader](const SpellTab& left, const SpellTab& right) {
        const bool left_is_general = left.skill_line_id == 0;
        const bool right_is_general = right.skill_line_id == 0;
        if (left_is_general != right_is_general) {
          return left_is_general;
        }
        if (left_is_general) {
          return false;
        }

        const auto left_name = ResolveSpellTabSortName(dbc_loader, left);
        const auto right_name = ResolveSpellTabSortName(dbc_loader, right);
        if (!left_name.has_value() || !right_name.has_value()) {
          return false;
        }

        return core::SStrCmpNoCaseCollate(left_name->data(), right_name->data(),
                                          kUnboundedStormStringCompare) < 0;
      });
}

std::optional<SpellSortMetadata> ResolveSpellSortMetadata(
    const SpellQueryBridge& query_bridge,
    const openwow::data::dbc::DbcLoader* dbc_loader,
    const SpellInfo& spell) {
  if (dbc_loader != nullptr) {
    if (const auto* spell_entry =
            dbc_loader->spell().LookupEntry(spell.spell_id);
        spell_entry != nullptr) {
      return SpellSortMetadata{
          .name = std::string(spell_entry->spell_name),
          .subtext = std::string(spell_entry->rank),
          .fallback_rank = spell.rank != 0
                               ? spell.rank
                               : static_cast<std::uint32_t>(
                                     spell_entry->spell_level),
      };
    }
  }

  if (const auto query = query_bridge.Query(spell.spell_id)) {
    return SpellSortMetadata{
        .name = query->name,
        .subtext = query->subtext,
        .fallback_rank = query->rank != 0 ? query->rank : spell.rank,
    };
  }
  return std::nullopt;
}

std::optional<std::pair<std::uint8_t, std::uint8_t>> GetActivePlayerIdentity(
    const ObjectManager& objects) {
  const auto* player = objects.GetActivePlayer();
  if (player == nullptr) {
    return std::nullopt;
  }

  return std::make_pair(player->State().GetRace(), player->State().GetClass());
}

}

std::optional<std::uint32_t> ResolveSpellSkillLineId(
    const ObjectManager& objects,
    const openwow::data::dbc::DbcLoader* dbc_loader,
    const std::uint8_t race,
    const std::uint8_t player_class,
    const std::uint32_t spell_id) {
  if (dbc_loader == nullptr || spell_id == 0 || race == 0 || player_class == 0) {
    return std::nullopt;
  }

  std::optional<SkillRaceClassIdentity> active_identity;
  if (const auto identity = GetActivePlayerIdentity(objects);
      identity.has_value()) {
    active_identity =
        SkillRaceClassIdentity{identity->first, identity->second};
  }

  const auto* ability = FindSkillLineAbilityForRaceClassSpell(
      dbc_loader->skill_line_ability().entries(),
      dbc_loader->skill_race_class_info().entries(), race, player_class,
      spell_id, active_identity);
  if (ability == nullptr) {
    return std::nullopt;
  }

  const auto* skill_info = FindSkillRaceClassInfoBySkillId(
      dbc_loader->skill_race_class_info().entries(), race, player_class,
      ability->skill_id);
  if (skill_info == nullptr ||
      (skill_info->flags & kHiddenSkillLineFlag) != 0) {
    return std::nullopt;
  }

  return ability->skill_id;
}

namespace {

std::optional<GatherInteractionPrimaryEffect> ResolveGatherInteractionPrimaryEffect(
    const SpellQueryBridge& query_bridge,
    const openwow::data::dbc::DbcLoader* dbc_loader,
    const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  if (const auto query = query_bridge.Query(spell_id); query.has_value()) {
    return GatherInteractionPrimaryEffect{
        query->effectIds[0],
        query->effectMiscValue[0],
    };
  }

  if (dbc_loader == nullptr) {
    return std::nullopt;
  }

  const auto* spell_entry = dbc_loader->spell().LookupEntry(spell_id);
  if (spell_entry == nullptr) {
    return std::nullopt;
  }

  return GatherInteractionPrimaryEffect{
      spell_entry->effect[0],
      spell_entry->effect_misc_value[0],
  };
}

}

SkinnableResourceType ResolveSkinnableResourceType(
    const CreatureTemplateInfo& creature_template) {
  if ((creature_template.type_flags & 0x100u) != 0) {
    return SkinnableResourceType::Herb;
  }
  if ((creature_template.type_flags & 0x200u) != 0) {
    return SkinnableResourceType::Rock;
  }
  if ((creature_template.type_flags & 0x8000u) != 0) {
    return SkinnableResourceType::Bolts;
  }

  return SkinnableResourceType::Leather;
}

SpellbookSystem& SpellbookSystem::Get() {
  static SpellbookSystem instance;
  return instance;
}

void SpellbookSystem::SetSpells(const ObjectManager& objects,
                                const std::vector<SpellInfo>& spells) {
  std::lock_guard lock(mutex_);
  spells_.clear();
  known_spell_list_ = spells;
  for (const auto& s : spells) {
    spells_[s.spell_id] = s;
  }
  RefreshTrackedSpellIdsLocked();
  RebuildDisplayStateLocked(objects);
}

void SpellbookSystem::AddSpell(const ObjectManager& objects,
                               const SpellInfo& spell) {
  std::lock_guard lock(mutex_);
  spells_[spell.spell_id] = spell;
  auto it = std::find_if(known_spell_list_.begin(), known_spell_list_.end(),
                         [&](const SpellInfo& s) {
                           return s.spell_id == spell.spell_id;
                         });
  if (it == known_spell_list_.end()) {
    known_spell_list_.push_back(spell);
  } else {
    *it = spell;
  }
  RefreshTrackedSpellIdsLocked();
  RebuildDisplayStateLocked(objects);
}

void SpellbookSystem::RemoveSpell(const ObjectManager& objects,
                                  uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  spells_.erase(spell_id);
  known_spell_list_.erase(
      std::remove_if(known_spell_list_.begin(), known_spell_list_.end(),
                     [spell_id](const SpellInfo& s) {
                       return s.spell_id == spell_id;
                     }),
      known_spell_list_.end());
  RefreshTrackedSpellIdsLocked();
  RebuildDisplayStateLocked(objects);
}

void SpellbookSystem::ReplaceSpell(const ObjectManager& objects,
                                   uint32_t old_spell_id,
                                   const SpellInfo& spell) {
  std::lock_guard lock(mutex_);

  if (old_spell_id != 0 && old_spell_id != spell.spell_id) {
    spells_.erase(old_spell_id);
  }
  spells_[spell.spell_id] = spell;

  auto replacement = std::find_if(
      known_spell_list_.begin(), known_spell_list_.end(),
      [old_spell_id](const SpellInfo& entry) {
        return entry.spell_id == old_spell_id;
      });
  if (replacement != known_spell_list_.end()) {
    *replacement = spell;
    RefreshTrackedSpellIdsLocked();
    RebuildDisplayStateLocked(objects);
    return;
  }

  auto existing = std::find_if(
      known_spell_list_.begin(), known_spell_list_.end(),
      [&spell](const SpellInfo& entry) {
        return entry.spell_id == spell.spell_id;
      });
  if (existing == known_spell_list_.end()) {
    known_spell_list_.push_back(spell);
    RefreshTrackedSpellIdsLocked();
    RebuildDisplayStateLocked(objects);
    return;
  }

  *existing = spell;
  RefreshTrackedSpellIdsLocked();
  RebuildDisplayStateLocked(objects);
}

bool SpellbookSystem::HasSpell(uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  return spells_.count(spell_id) > 0;
}

bool SpellbookSystem::HasSpellOrSupersedingRank(
    const uint32_t spell_id, const std::uint8_t race,
    const std::uint8_t player_class) const {
  std::lock_guard lock(mutex_);

  if (spells_.contains(spell_id)) {
    return true;
  }

  const auto mask_matches = [](const std::uint32_t mask,
                               const std::uint8_t value) {
    if (mask == 0) {
      return true;
    }
    const auto shift = (static_cast<std::uint32_t>(value) - 1u) & 31u;
    return (mask & (std::uint32_t{1} << shift)) != 0;
  };
  const auto skill_is_available = [&](const std::uint32_t skill_id) {
    const auto eligibility = supersession_skill_eligibility_.find(skill_id);
    if (eligibility == supersession_skill_eligibility_.end()) {
      return false;
    }
    return std::any_of(
        eligibility->second.begin(), eligibility->second.end(),
        [&](const SupersessionSkillEligibility& entry) {
          return mask_matches(entry.race_mask, race) &&
                 mask_matches(entry.class_mask, player_class);
        });
  };
  const auto resolve_next_spell = [&](const std::uint32_t current_spell)
      -> std::optional<std::uint32_t> {
    const auto abilities =
        supersession_abilities_by_spell_.find(current_spell);
    if (abilities == supersession_abilities_by_spell_.end()) {
      return std::nullopt;
    }

    std::optional<std::uint32_t> next_spell;
    for (const auto& ability : abilities->second) {
      const auto race_mask = ability.exclude_race_mask != 0
                                 ? ~ability.race_mask
                                 : ability.race_mask;
      const auto class_mask = ability.exclude_class_mask != 0
                                  ? ~ability.class_mask
                                  : ability.class_mask;
      if (!mask_matches(race_mask, race) ||
          !mask_matches(class_mask, player_class) ||
          !skill_is_available(ability.skill_id)) {
        continue;
      }

      next_spell = ability.next_spell;
    }
    return next_spell;
  };

  auto current_spell = spell_id;

  auto remaining_nodes = supersession_abilities_by_spell_.size();
  while (remaining_nodes-- != 0) {
    const auto next_spell_value = resolve_next_spell(current_spell);
    if (!next_spell_value.has_value()) {
      return false;
    }

    const auto next_spell = *next_spell_value;

    if (next_spell == 0 ||
        next_spell > static_cast<std::uint32_t>(
                         std::numeric_limits<std::int32_t>::max())) {
      return false;
    }

    if (spells_.contains(next_spell)) {
      return true;
    }
    current_spell = next_spell;
  }

  return false;
}

size_t SpellbookSystem::GetNumSpells() const {
  std::lock_guard lock(mutex_);
  return spells_.size();
}

const SpellInfo* SpellbookSystem::GetSpell(uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  auto it = spells_.find(spell_id);
  if (it == spells_.end()) return nullptr;
  return &it->second;
}

std::uint32_t SpellbookSystem::GetAutoRangedCombatSpellId() const {
  std::lock_guard lock(mutex_);
  return auto_ranged_combat_spell_id_;
}

const std::vector<SpellInfo>& SpellbookSystem::GetKnownSpellList() const {

  return known_spell_list_;
}

const std::vector<SpellInfo>& SpellbookSystem::GetSpellList() const {

  return spell_list_;
}

std::vector<std::uint32_t> SpellbookSystem::GetCompanionSpellList(
    const CompanionSpellType type) const {
  std::lock_guard lock(mutex_);
  return companion_spell_lists_[static_cast<std::size_t>(type)];
}

void SpellbookSystem::SetCompanionSpellListOrder(
    const CompanionSpellType type, std::vector<std::uint32_t> spell_ids) {
  std::lock_guard lock(mutex_);
  companion_spell_lists_[static_cast<std::size_t>(type)] =
      std::move(spell_ids);
}

bool SpellbookSystem::SortCompanionSpellLists(
    const QueryCache& query_cache) {
  std::lock_guard lock(mutex_);
  if (dbc_loader_ == nullptr) {
    return false;
  }

  bool processed_nonempty_list = false;
  for (auto& spell_ids : companion_spell_lists_) {
    if (spell_ids.empty()) {
      continue;
    }
    processed_nonempty_list = true;

    std::vector<CompanionSortRecord> prepared;
    prepared.reserve(spell_ids.size());
    for (const auto spell_id : spell_ids) {
      prepared.push_back({
          .spell_id = spell_id,
          .creature_name =
              ResolveCompanionCreatureName(*dbc_loader_, query_cache, spell_id),
      });
    }

    std::stable_sort(
        prepared.begin(), prepared.end(),
        [](const CompanionSortRecord& left,
           const CompanionSortRecord& right) {
          if (left.creature_name.has_value() !=
              right.creature_name.has_value()) {
            return left.creature_name.has_value();
          }
          if (!left.creature_name.has_value()) {
            return false;
          }
          return core::SStrCmpNoCaseCollate(
                     left.creature_name->c_str(), right.creature_name->c_str(),
                     kUnboundedStormStringCompare) < 0;
        });
    std::transform(prepared.begin(), prepared.end(), spell_ids.begin(),
                   [](const CompanionSortRecord& record) {
                     return record.spell_id;
                   });
  }
  return processed_nonempty_list;
}

std::optional<std::uint64_t> SpellbookSystem::BeginCompanionNameQuery(
    const std::uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  if (spell_id == 0 || pending_companion_name_queries_.contains(spell_id)) {
    return std::nullopt;
  }

  auto token = next_companion_name_query_token_++;
  if (token == 0) {
    token = next_companion_name_query_token_++;
  }
  pending_companion_name_queries_[spell_id] = token;
  return token;
}

bool SpellbookSystem::CompleteCompanionNameQuery(
    const std::uint32_t spell_id, const std::uint64_t token) {
  std::lock_guard lock(mutex_);
  const auto pending = pending_companion_name_queries_.find(spell_id);
  if (pending == pending_companion_name_queries_.end() ||
      pending->second != token) {
    return false;
  }
  pending_companion_name_queries_.erase(pending);
  return true;
}

bool SpellbookSystem::HasPendingCompanionNameQueries() const {
  std::lock_guard lock(mutex_);
  return !pending_companion_name_queries_.empty();
}

void SpellbookSystem::ClearCompanionSpellLists() {
  std::lock_guard lock(mutex_);
  for (auto& list : companion_spell_lists_) {
    list.clear();
  }
  pending_companion_name_queries_.clear();
}

void SpellbookSystem::QueueUiEvent(const SpellbookUiEventType type,
                                   const std::uint32_t argument) {
  std::lock_guard lock(mutex_);
  pending_ui_events_.push_back({type, argument});
}

void SpellbookSystem::QueueLearnedSpellInTab(
    const std::uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  const auto spell = std::find_if(
      spell_list_.begin(), spell_list_.end(),
      [spell_id](const SpellInfo& entry) {
        return entry.spell_id == spell_id;
      });
  if (spell == spell_list_.end()) {
    return;
  }

  const auto tab = std::find_if(
      tabs_.begin(), tabs_.end(),
      [skill_line_id = spell->skill_line_id](const SpellTab& entry) {
        return entry.skill_line_id == skill_line_id;
      });
  const auto tab_index =
      tab != tabs_.end()
          ? static_cast<std::uint32_t>(std::distance(tabs_.begin(), tab) + 1)
          : 0u;
  pending_ui_events_.push_back(
      {SpellbookUiEventType::LearnedSpellInTab, tab_index});
}

std::vector<SpellbookUiEvent> SpellbookSystem::ConsumeUiEvents() {
  std::lock_guard lock(mutex_);
  std::vector<SpellbookUiEvent> events;
  events.swap(pending_ui_events_);
  return events;
}

void SpellbookSystem::SetDbcLoader(
    const ::openwow::data::dbc::DbcLoader* dbc_loader,
    const ObjectManager& objects) {
  std::lock_guard lock(mutex_);
  if (dbc_loader_ != dbc_loader) {
    pending_companion_name_queries_.clear();
  }
  dbc_loader_ = dbc_loader;
  RebuildSupersessionIndexLocked();
  SortSpellTabsForDisplay(&tabs_, dbc_loader_);
  RefreshTrackedSpellIdsLocked();
  RebuildDisplayStateLocked(objects);
}

void SpellbookSystem::SetTabs(const ObjectManager& objects,
                              const std::vector<SpellTab>& tabs) {
  std::lock_guard lock(mutex_);
  tabs_ = tabs;
  has_explicit_tabs_ = true;
  SortSpellTabsForDisplay(&tabs_, dbc_loader_);
  RebuildDisplayStateLocked(objects);
}

size_t SpellbookSystem::GetNumTabs() const {
  std::lock_guard lock(mutex_);
  return tabs_.size();
}

const SpellTab* SpellbookSystem::GetTab(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= tabs_.size()) return nullptr;
  return &tabs_[index];
}

const SpellInfo* SpellbookSystem::GetPlayerSpellBookSlot(
    const uint32_t slot) const {
  std::lock_guard lock(mutex_);

  if (slot == 0 || slot > spell_list_.size()) return nullptr;
  return &spell_list_[slot - 1];
}

size_t SpellbookSystem::GetPlayerSpellBookSlotCount() const {
  std::lock_guard lock(mutex_);
  return spell_list_.size();
}

std::uint32_t SpellbookSystem::GetKnownSlotFromHighestRankSlot(
    const std::uint32_t highest_rank_slot) const {
  std::lock_guard lock(mutex_);

  const std::uint32_t zero_based = highest_rank_slot - 1u;
  if (highest_rank_slot == 0u || zero_based >= highest_rank_spell_ids_.size()) {
    return 0;
  }

  const std::uint32_t spell_id = highest_rank_spell_ids_[zero_based];
  for (std::size_t index = 0; index < spell_list_.size(); ++index) {
    if (spell_list_[index].spell_id == spell_id) {
      return static_cast<std::uint32_t>(index + 1);
    }
  }
  return 0;
}

void SpellbookSystem::RefreshDisplayState(const ObjectManager& objects) {
  std::lock_guard lock(mutex_);
  RebuildDisplayStateLocked(objects);
}

std::uint32_t SpellbookSystem::FindKnownSpellByCategory(
    const std::uint32_t category) const {
  std::lock_guard lock(mutex_);
  if (category == 0 || dbc_loader_ == nullptr) {
    return 0;
  }

  for (const auto& spell : known_spell_list_) {
    const auto* entry = dbc_loader_->spell().LookupEntry(spell.spell_id);
    if (entry != nullptr && entry->category == category) {
      return spell.spell_id;
    }
  }

  return 0;
}

std::uint32_t SpellbookSystem::GetSummonFriendSpellId() const {
  std::lock_guard lock(mutex_);
  return summon_friend_spell_id_;
}

std::uint32_t SpellbookSystem::GetCorpseOrPlayerGatherInteractionSpellId() const {
  std::lock_guard lock(mutex_);
  return corpse_or_player_gather_interaction_spell_id_;
}

std::uint32_t SpellbookSystem::GetSkinnableGatherInteractionSpellId(
    const SkinnableResourceType type) const {
  std::lock_guard lock(mutex_);
  return skinnable_gather_interaction_spell_ids_[static_cast<std::size_t>(type)];
}

std::uint32_t SpellbookSystem::ResolveGatherInteractionSpellId(
    const CGObject_C& target,
    const QueryCache* query_cache) const {
  if (target.IsPlayer() || target.IsCorpse()) {
    std::lock_guard lock(mutex_);
    return corpse_or_player_gather_interaction_spell_id_;
  }

  if (!target.IsUnit() || query_cache == nullptr) {
    return 0;
  }

  const auto* creature_template =
      query_cache->GetCreatureTemplate(target.GetEntry());
  if (creature_template == nullptr) {
    return 0;
  }

  const auto resource_type = ResolveSkinnableResourceType(*creature_template);
  std::lock_guard lock(mutex_);
  return skinnable_gather_interaction_spell_ids_[static_cast<std::size_t>(resource_type)];
}

void SpellbookSystem::TrackLearnedGatherInteractionSpell(
    const std::uint32_t spell_id) {
  const openwow::data::dbc::DbcLoader* dbc_loader = nullptr;
  {
    std::lock_guard lock(mutex_);
    dbc_loader = dbc_loader_;
  }

  const auto effect = ResolveGatherInteractionPrimaryEffect(
      SpellQueryBridge::Get(), dbc_loader, spell_id);
  if (!effect.has_value()) {
    return;
  }

  std::lock_guard lock(mutex_);
  switch (effect->effect_id) {
    case kSkinnableGatherInteractionEffectId:
      if (effect->misc_value >= 0 &&
          static_cast<std::size_t>(effect->misc_value) <
              skinnable_gather_interaction_spell_ids_.size()) {
        skinnable_gather_interaction_spell_ids_[static_cast<std::size_t>(effect->misc_value)] =
            spell_id;
      }
      break;
    case kCorpseOrPlayerGatherInteractionEffectId:
      corpse_or_player_gather_interaction_spell_id_ = spell_id;
      break;
    default:
      break;
  }
}

void SpellbookSystem::ForgetGatherInteractionSpell(
    const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return;
  }

  std::lock_guard lock(mutex_);
  for (auto& tracked_spell_id : skinnable_gather_interaction_spell_ids_) {
    if (tracked_spell_id == spell_id) {
      tracked_spell_id = 0;
    }
  }

  if (corpse_or_player_gather_interaction_spell_id_ == spell_id) {
    corpse_or_player_gather_interaction_spell_id_ = 0;
  }
}

void SpellbookSystem::SetCooldown(uint32_t spell_id, uint32_t remaining_ms,
                                  uint32_t duration_ms) {
  std::lock_guard lock(mutex_);
  cooldowns_[spell_id] = {remaining_ms, duration_ms};
}

bool SpellbookSystem::IsOnCooldown(uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  auto it = cooldowns_.find(spell_id);
  if (it == cooldowns_.end()) return false;
  return it->second.first > 0;
}

std::pair<uint32_t, uint32_t> SpellbookSystem::GetCooldown(
    uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  auto it = cooldowns_.find(spell_id);
  if (it == cooldowns_.end()) return {0, 0};
  return it->second;
}

void SpellbookSystem::ClearCooldown(uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  cooldowns_.erase(spell_id);
}

void SpellbookSystem::ClearAllCooldowns() {
  std::lock_guard lock(mutex_);
  cooldowns_.clear();
}

void SpellbookSystem::ResetSpellbookData() {
  std::lock_guard lock(mutex_);
  spells_.clear();
  known_spell_list_.clear();
  spell_list_.clear();
  highest_rank_spell_ids_.clear();
  tabs_.clear();
  for (auto& list : companion_spell_lists_) {
    list.clear();
  }
  pending_companion_name_queries_.clear();
  pending_ui_events_.clear();
  has_explicit_tabs_ = false;
  cooldowns_.clear();
  summon_friend_spell_id_ = 0;
  auto_ranged_combat_spell_id_ = 0;
  skinnable_gather_interaction_spell_ids_.fill(0);
  corpse_or_player_gather_interaction_spell_id_ = 0;
}

void SpellbookSystem::Reset() {
  {
    std::lock_guard lock(mutex_);
    dbc_loader_ = nullptr;
    spells_.clear();
    supersession_abilities_by_spell_.clear();
    supersession_skill_eligibility_.clear();
    known_spell_list_.clear();
    spell_list_.clear();
    highest_rank_spell_ids_.clear();
    tabs_.clear();
    for (auto& list : companion_spell_lists_) {
      list.clear();
    }
    pending_companion_name_queries_.clear();
    pending_ui_events_.clear();
    has_explicit_tabs_ = false;
    cooldowns_.clear();
    summon_friend_spell_id_ = 0;
    auto_ranged_combat_spell_id_ = 0;
    skinnable_gather_interaction_spell_ids_.fill(0);
    corpse_or_player_gather_interaction_spell_id_ = 0;
  }

  SpellBookFrame::ClearSpellGroups();
}

std::uint32_t SpellbookSystem::ResolveSpellFamilyNameLocked(
    const std::uint32_t spell_id) const {
  if (spell_id == 0) {
    return 0;
  }

  if (dbc_loader_ != nullptr) {
    if (const auto* spell_entry = dbc_loader_->spell().LookupEntry(spell_id);
        spell_entry != nullptr) {
      return spell_entry->spell_family_name;
    }
  }

  if (const auto spell = SpellQueryBridge::Get().Query(spell_id);
      spell.has_value()) {
    return spell->spellFamilyName;
  }
  return 0;
}

bool SpellbookSystem::IsAutoRangedCombatSpellLocked(
    const std::uint32_t spell_id) const {
  if (spell_id == 0) {
    return false;
  }

  if (dbc_loader_ != nullptr) {
    if (const auto* spell_entry = dbc_loader_->spell().LookupEntry(spell_id);
        spell_entry != nullptr) {
      return (spell_entry->attributes_ex4 &
              kSpellAttrEx4AutoRangedCombat) != 0u;
    }
  }

  if (const auto query = SpellQueryBridge::Get().Query(spell_id);
      query.has_value()) {
    return (query->attributesEx4 & kSpellAttrEx4AutoRangedCombat) != 0u;
  }
  return false;
}

void SpellbookSystem::RebuildSupersessionIndexLocked() {
  supersession_abilities_by_spell_.clear();
  supersession_skill_eligibility_.clear();
  if (dbc_loader_ == nullptr) {
    return;
  }

  supersession_skill_eligibility_.reserve(
      dbc_loader_->skill_race_class_info().entries().size());
  for (const auto& skill_info :
       dbc_loader_->skill_race_class_info().entries()) {
    supersession_skill_eligibility_[skill_info.skill_id].push_back({
        .race_mask = skill_info.race_mask,
        .class_mask = skill_info.class_mask,
    });
  }

  supersession_abilities_by_spell_.reserve(
      dbc_loader_->skill_line_ability().entries().size());
  for (const auto& ability : dbc_loader_->skill_line_ability().entries()) {
    supersession_abilities_by_spell_[ability.spell_id].push_back({
        .skill_id = ability.skill_id,
        .race_mask = ability.race_mask,
        .class_mask = ability.class_mask,
        .exclude_race_mask = ability.exclude_race_mask,
        .exclude_class_mask = ability.exclude_class_mask,
        .next_spell = ability.superseded_by_spell,
    });
  }
}

void SpellbookSystem::RefreshTrackedSpellIdsLocked() {
  summon_friend_spell_id_ = 0;
  auto_ranged_combat_spell_id_ = 0;

  for (const auto& spell : known_spell_list_) {
    if (IsAutoRangedCombatSpellLocked(spell.spell_id)) {
      auto_ranged_combat_spell_id_ = spell.spell_id;
    }
  }

  for (const auto& spell : known_spell_list_) {
    if (ResolveSpellFamilyNameLocked(spell.spell_id) ==
        kReferAFriendSummonSpellFamilyName) {
      summon_friend_spell_id_ = spell.spell_id;
    }
  }

  if (summon_friend_spell_id_ != 0) {
    return;
  }

  for (const auto& [spell_id, spell] : spells_) {
    static_cast<void>(spell);
    if (ResolveSpellFamilyNameLocked(spell_id) ==
        kReferAFriendSummonSpellFamilyName) {
      summon_friend_spell_id_ = spell_id;
      return;
    }
  }
}

void SpellbookSystem::RebuildSpellTabsLocked() {
  if (has_explicit_tabs_ || dbc_loader_ == nullptr) {
    return;
  }

  tabs_.clear();
  tabs_.reserve(spell_list_.size());

  const auto add_tab = [this](const std::uint32_t skill_line_id) {
    if (std::any_of(tabs_.begin(), tabs_.end(),
                    [skill_line_id](const SpellTab& tab) {
                      return tab.skill_line_id == skill_line_id;
                    })) {
      return;
    }

    if (skill_line_id == 0) {
      tabs_.push_back(SpellTab{.name = std::string(kGeneralSpellTabName),
                               .texture = std::string(kGeneralSpellTabTexture),
                               .skill_line_id = 0});
      return;
    }

    const auto* skill_line = dbc_loader_->skill_line().LookupEntry(skill_line_id);
    std::string name;
    std::string texture;
    if (skill_line != nullptr) {
      name = std::string(skill_line->name);
      if (const auto* icon =
              dbc_loader_->spell_icon().LookupEntry(skill_line->spell_icon_id);
          icon != nullptr) {
        texture = icon->icon_path;
      }
    }
    tabs_.push_back(SpellTab{.name = std::move(name),
                             .texture = std::move(texture),
                             .skill_line_id = skill_line_id});
  };

  for (const auto& spell : spell_list_) {
    add_tab(spell.skill_line_id);
  }
  SortSpellTabsForDisplay(&tabs_, dbc_loader_);
}

void SpellbookSystem::RebuildDisplayStateLocked(const ObjectManager& objects) {
  highest_rank_spell_ids_.clear();
  spell_list_.clear();
  std::array<std::vector<std::uint32_t>, 2> classified_companions;

  const auto append_companion = [&classified_companions](
                                     const CompanionSpellType type,
                                     const std::uint32_t spell_id) {
    auto& list = classified_companions[static_cast<std::size_t>(type)];
    if (std::find(list.begin(), list.end(), spell_id) == list.end()) {
      list.push_back(spell_id);
    }
  };

  for (const auto& known_spell : known_spell_list_) {
    if (dbc_loader_ == nullptr) {
      spell_list_.push_back(known_spell);
      continue;
    }

    const auto* spell_entry =
        dbc_loader_->spell().LookupEntry(known_spell.spell_id);
    if (spell_entry == nullptr) {

      spell_list_.push_back(known_spell);
      continue;
    }

    switch (ClassifyLearnedSpell(*spell_entry, *dbc_loader_)) {
      case LearnedSpellCatalog::Spellbook:
        spell_list_.push_back(known_spell);
        break;
      case LearnedSpellCatalog::Critter:
        append_companion(CompanionSpellType::Critter, known_spell.spell_id);
        break;
      case LearnedSpellCatalog::Mount:
        append_companion(CompanionSpellType::Mount, known_spell.spell_id);
        break;
      case LearnedSpellCatalog::TradeSkill:
      case LearnedSpellCatalog::HiddenClientside:
      case LearnedSpellCatalog::HiddenSpellbook:
        break;
    }
  }

  for (std::size_t type = 0; type < companion_spell_lists_.size(); ++type) {
    const auto& classified = classified_companions[type];
    std::vector<std::uint32_t> ordered;
    ordered.reserve(classified.size());
    for (const auto spell_id : companion_spell_lists_[type]) {
      if (std::find(classified.begin(), classified.end(), spell_id) !=
              classified.end() &&
          std::find(ordered.begin(), ordered.end(), spell_id) == ordered.end()) {
        ordered.push_back(spell_id);
      }
    }
    for (const auto spell_id : classified) {
      if (std::find(ordered.begin(), ordered.end(), spell_id) == ordered.end()) {
        ordered.push_back(spell_id);
      }
    }
    companion_spell_lists_[type] = std::move(ordered);
  }

  if (spell_list_.empty()) {
    if (!has_explicit_tabs_) {
      tabs_.clear();
    }
    return;
  }

  SpellQueryBridge& query_bridge = SpellQueryBridge::Get();
  const auto active_player = GetActivePlayerIdentity(objects);

  for (auto& spell : spell_list_) {
    std::uint32_t resolved_skill_line_id =
        active_player.has_value() ? 0 : spell.skill_line_id;
    if (active_player.has_value()) {
      if (const auto resolved_skill_line = ResolveSpellSkillLineId(
              objects, dbc_loader_, active_player->first,
              active_player->second, spell.spell_id)) {
        resolved_skill_line_id = *resolved_skill_line;
      }
    }

    spell.skill_line_id = resolved_skill_line_id;
  }

  RebuildSpellTabsLocked();
  SortSpellTabsForDisplay(&tabs_, dbc_loader_);
  for (auto& tab : tabs_) {
    tab.num_known.reset();
  }

  auto find_tab_index = [&](const std::uint32_t skill_line_id) -> std::size_t {
    for (std::size_t index = 0; index < tabs_.size(); ++index) {
      if (tabs_[index].skill_line_id == skill_line_id) {
        return index;
      }
    }
    return tabs_.size();
  };

  std::stable_sort(spell_list_.begin(), spell_list_.end(),
                   [&](const SpellInfo& left, const SpellInfo& right) {
                     const auto left_tab_index = find_tab_index(left.skill_line_id);
                     const auto right_tab_index = find_tab_index(right.skill_line_id);
                     if (left_tab_index != right_tab_index) {
                       return left_tab_index < right_tab_index;
                     }

                     const auto left_metadata =
                         ResolveSpellSortMetadata(query_bridge, dbc_loader_, left);
                     const auto right_metadata =
                         ResolveSpellSortMetadata(query_bridge, dbc_loader_, right);
                     if (!left_metadata || !right_metadata) {
                       return false;
                     }

                     const std::string left_name(left_metadata->name);
                     const std::string right_name(right_metadata->name);
                     const auto name_compare = core::SStrCmpNoCaseCollate(
                         left_name.c_str(), right_name.c_str(),
                         kUnboundedStormStringCompare);
                     if (name_compare != 0) {
                       return name_compare < 0;
                     }

                     const auto left_rank =
                         ResolveSpellProgressionRank(left_metadata->subtext,
                                                     left_metadata->fallback_rank);
                     const auto right_rank =
                         ResolveSpellProgressionRank(right_metadata->subtext,
                                                     right_metadata->fallback_rank);
                     if (left_rank != 0 && right_rank != 0 && left_rank != right_rank) {
                       return left_rank < right_rank;
                     }

                     const std::string left_subtext(left_metadata->subtext);
                     const std::string right_subtext(right_metadata->subtext);
                     return core::SStrCmpNoCaseCollate(
                                left_subtext.c_str(),
                                right_subtext.c_str(),
                                kUnboundedStormStringCompare) < 0;
                   });

  if (tabs_.empty()) {
    return;
  }

  auto spell_tab_index = std::vector<std::size_t>{};
  spell_tab_index.reserve(spell_list_.size());
  for (const auto& spell : spell_list_) {
    spell_tab_index.push_back(find_tab_index(spell.skill_line_id));
  }

  std::uint32_t running_offset = 0;
  for (std::size_t tab_index = 0; tab_index < tabs_.size(); ++tab_index) {
    auto& tab = tabs_[tab_index];
    tab.offset = running_offset;
    tab.num_spells = static_cast<std::uint32_t>(std::count(
        spell_tab_index.begin(), spell_tab_index.end(), tab_index));
    running_offset += tab.num_spells;
  }

  for (auto& tab : tabs_) {
    tab.highest_rank_offset =
        static_cast<std::uint32_t>(highest_rank_spell_ids_.size());
    std::uint32_t known_count = 0;
    const auto tab_begin = static_cast<std::size_t>(tab.offset);
    const auto tab_end = std::min(
        spell_list_.size(), tab_begin + static_cast<std::size_t>(tab.num_spells));
    for (std::size_t spell_index = tab_begin;
         spell_index < tab_end;
         ++spell_index) {
      const auto current_metadata = ResolveSpellSortMetadata(
          query_bridge, dbc_loader_, spell_list_[spell_index]);
      if (!current_metadata.has_value()) {
        continue;
      }
      bool emit_highest_rank_slot = spell_index + 1 == tab_end;
      if (!emit_highest_rank_slot) {
        const auto next_metadata = ResolveSpellSortMetadata(
            query_bridge, dbc_loader_, spell_list_[spell_index + 1]);
        if (!next_metadata.has_value()) {
          emit_highest_rank_slot = true;
        } else {
          const bool same_name = core::SStrCmpNoCaseCollate(
                                     current_metadata->name.c_str(),
                                     next_metadata->name.c_str(),
                                     kUnboundedStormStringCompare) == 0;
          const bool next_has_explicit_rank =
              ParseFirstAsciiDigitRun(next_metadata->subtext).has_value();
          emit_highest_rank_slot = !same_name || !next_has_explicit_rank;
        }
      }

      if (emit_highest_rank_slot) {
        highest_rank_spell_ids_.push_back(spell_list_[spell_index].spell_id);
        ++known_count;
      }
    }

    tab.num_known = known_count;
  }

}

std::uint32_t GetAutoSelfCastAttackSpellId() {

  if (!GameSettings::Get().IsAutoSelfCast()) {
    return 0;
  }
  return SpellbookSystem::Get().GetAutoRangedCombatSpellId();
}

}

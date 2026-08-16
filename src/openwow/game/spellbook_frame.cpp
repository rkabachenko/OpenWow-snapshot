
#include "openwow/game/spellbook_frame.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/pet_manager.h"
#include "openwow/game/spell_shapeshift_mask.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_learning_reference.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace openwow::game {

std::vector<SpellGroup> SpellBookFrame::s_spell_groups;

namespace {

constexpr std::size_t kUnboundedStormStringCompare = 0x7FFFFFFFu;
constexpr std::uint8_t kWarriorClassId = 1;
constexpr std::uint8_t kRogueClassId = 4;
constexpr std::uint8_t kDruidClassId = 11;
constexpr std::uint32_t kAutoPlaceBarSlotCount = 12;
constexpr std::uint32_t kAutoPlaceActionBarOffsetBias = 5;
constexpr std::uint32_t kSpellAttr0Ability = 0x10u;
constexpr std::uint32_t kSpellAttr0Passive = 0x40u;
constexpr std::uint32_t kSpellAttr0NoDownrank = 0x04000000u;
constexpr std::uint32_t kSpellAttrEx2DownrankSkip = 0x08u;
constexpr std::int32_t kDownrankLevelBuffer = 10;
constexpr std::uint32_t kSpellAttr1LearnSpellAutoPlaceSuppress = 0x10u;
constexpr std::uint32_t kSpellAttr2SkipStanceAutoPlace = 0x80000u;
constexpr std::uint32_t kShapeshiftAuraType = 36u;

bool StormUtf8NoCaseEquals(std::string_view lhs, std::string_view rhs) {
  const std::string lhs_string(lhs);
  const std::string rhs_string(rhs);
  return core::SStrCmpUTF8NoCase(lhs_string.c_str(), rhs_string.c_str(),
                                 kUnboundedStormStringCompare) == 0;
}

bool SpellMatchesMultiCastTotemSlot(
    const data::dbc::SpellEntry& spell,
    const std::uint8_t slot_index) {
  const auto slot_mask = 1u << slot_index;
  for (const auto category : spell.totem_category) {
    if ((SpellBookFrame::MultiCastTotemCategoryToSlotMask(category) & slot_mask) != 0) {
      return true;
    }
  }

  return false;
}

struct MultiCastTotemSpellChoice {
  std::uint32_t spell_id = 0;
  std::uint32_t sort_key = 0;
  std::string name;
};

template <typename Visitor>
bool VisitTrackedCarriedItemEntries(const PlayerInventoryReplica& inventory,
                                    Visitor&& visitor) {
  return !inventory.VisitDefaultPlayerItems(
      [&visitor](const ItemInstance& item) { return !visitor(item.entry); });
}

bool IsTrackedCarriedItemEntry(const PlayerInventoryReplica& inventory,
                               const std::uint32_t item_entry) {
  if (item_entry == 0) {
    return false;
  }

  return VisitTrackedCarriedItemEntries(
      inventory,
      [item_entry](const std::uint32_t carried_entry) {
        return carried_entry == item_entry;
      });
}

template <typename GroupContainer>
auto FindSpellGroup(GroupContainer& groups, std::string_view spell_name)
  -> typename GroupContainer::value_type* {
  const std::string spell_name_string(spell_name);
  const auto name_hash = core::SStrHashCI(spell_name_string.c_str());
  for (auto& group : groups) {
    if (group.name_hash == name_hash &&
        StormUtf8NoCaseEquals(group.name, spell_name_string)) {
      return &group;
    }
  }
  return nullptr;
}

template <typename Predicate>
void RemoveSpellGroupEntries(std::vector<SpellGroup>& groups,
                             Predicate&& should_remove) {
  groups.erase(
      std::remove_if(groups.begin(), groups.end(),
                     [&should_remove](SpellGroup& group) {
                       group.entries.erase(
                           std::remove_if(group.entries.begin(), group.entries.end(),
                                          [&should_remove](const SpellGroupEntry& entry) {
                                            return should_remove(entry);
                                          }),
                           group.entries.end());
                       return group.entries.empty();
                     }),
      groups.end());
}

void RemoveSpellGroupEntry(std::vector<SpellGroup>& groups,
                           const std::uint32_t spell_id,
                           const bool from_pet_spellbook) {
  RemoveSpellGroupEntries(
      groups,
      [spell_id, from_pet_spellbook](const SpellGroupEntry& entry) {
        return entry.spell_id == spell_id &&
               entry.from_pet_spellbook == from_pet_spellbook;
      });
}

SpellInfo BuildSpellInfo(std::uint32_t spell_id) {
  SpellInfo info;
  info.spell_id = spell_id;
  info.is_known = true;

  if (const auto spell = SpellQueryBridge::Get().Query(spell_id)) {
    info.rank = spell->rank;
    info.is_passive = spell->isPassive;
  }

  return info;
}

std::string FormatLearnedSpellText(const std::string_view name,
                                   const std::string_view subtext) {
  if (subtext.empty()) {
    return std::string{name};
  }

  std::string formatted{name};
  formatted += " (";
  formatted += subtext;
  formatted += ')';
  return formatted;
}

std::string ResolveSpellTrackingName(
    const std::uint32_t spell_id,
    const data::dbc::DbcLoader* const dbc_loader) {
  if (const auto query = SpellQueryBridge::Get().Query(spell_id);
      query.has_value() && !query->name.empty()) {
    return query->name;
  }

  if (dbc_loader != nullptr) {
    if (const auto* spell = dbc_loader->spell().LookupEntry(spell_id);
        spell != nullptr && !spell->spell_name.empty()) {
      return std::string{spell->spell_name};
    }
  }

  return {};
}

std::string ResolveSpellTrackingIconPath(
    const std::uint32_t spell_id,
    const data::dbc::DbcLoader* const dbc_loader) {
  if (dbc_loader == nullptr) {
    return {};
  }

  const auto* spell = dbc_loader->spell().LookupEntry(spell_id);
  if (spell == nullptr || spell->spell_icon_id == 0) {
    return {};
  }

  const auto* icon = dbc_loader->spell_icon().LookupEntry(spell->spell_icon_id);
  if (icon == nullptr || icon->icon_path.empty()) {
    return {};
  }

  return std::string{icon->icon_path};
}

std::array<std::uint32_t, 3> ResolveSpellTrackingAuraTypes(
    const std::uint32_t spell_id,
    const data::dbc::DbcLoader* const dbc_loader) {
  std::array<std::uint32_t, 3> effect_apply_aura{};

  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {
    effect_apply_aura = query->effectApplyAura;
  }

  if (dbc_loader != nullptr) {
    if (const auto* spell = dbc_loader->spell().LookupEntry(spell_id);
        spell != nullptr) {
      effect_apply_aura = spell->effect_apply_aura;
    }
  }

  return effect_apply_aura;
}

std::optional<TrackingEntry> BuildLearnedTrackingSpellEntry(
    const std::uint32_t spell_id,
    const data::dbc::DbcLoader* const dbc_loader) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  const auto effect_apply_aura =
      ResolveSpellTrackingAuraTypes(spell_id, dbc_loader);
  if (!Spell_HasTrackingAuraType(effect_apply_aura.data())) {
    return std::nullopt;
  }

  TrackingEntry entry{};
  entry.category = TrackingCategory::Unknown;
  entry.spellId = spell_id;
  entry.name = ResolveSpellTrackingName(spell_id, dbc_loader);
  entry.iconPath = ResolveSpellTrackingIconPath(spell_id, dbc_loader);
  return entry;
}

LearnedSpellCatalog ResolveLearnedSpellCatalog(
    const SpellbookSystem& system, const std::uint32_t spell_id) {
  const auto* dbc_loader = system.GetDbcLoader();
  const auto* spell_entry =
      dbc_loader != nullptr ? dbc_loader->spell().LookupEntry(spell_id) : nullptr;
  return spell_entry != nullptr
             ? ClassifyLearnedSpell(*spell_entry, *dbc_loader)
             : LearnedSpellCatalog::Spellbook;
}

bool IsCompanionCatalog(const LearnedSpellCatalog catalog) {
  return catalog == LearnedSpellCatalog::Critter ||
         catalog == LearnedSpellCatalog::Mount;
}

void FinalizeCompanionCatalogIfReady(const WorldSession& session) {
  auto& system = SpellbookSystem::Get();
  if (system.HasPendingCompanionNameQueries()) {
    return;
  }
  if (system.SortCompanionSpellLists(session.query_cache())) {
    system.QueueUiEvent(SpellbookUiEventType::CompanionUpdate);
  }
}

void FireCompanionCatalogMutationEvent(const bool learned) {
  SpellbookSystem::Get().QueueUiEvent(
      learned ? SpellbookUiEventType::CompanionLearned
              : SpellbookUiEventType::CompanionUnlearned);
}

void FinishCompanionNameQuery(WorldSession& session,
                              const std::uint32_t spell_id,
                              const std::uint32_t creature_entry,
                              const std::uint64_t token,
                              const bool show_learn_message,
                              const bool success) {
  auto& system = SpellbookSystem::Get();
  if (!system.CompleteCompanionNameQuery(spell_id, token)) {
    return;
  }

  if (show_learn_message && success) {
    if (const auto* creature =
            session.query_cache().GetCreatureTemplate(creature_entry);
        creature != nullptr) {
      ui::game::DisplaySystemMessage(62, creature->name.c_str());
      FinalizeCompanionCatalogIfReady(session);
      FireCompanionCatalogMutationEvent(true);
      return;
    }
  }
  FinalizeCompanionCatalogIfReady(session);
}

void RouteLearnedCompanion(WorldSession& session,
                           const std::uint32_t spell_id,
                           const LearnedSpellCatalog catalog,
                           const bool show_learn_message) {
  if (!IsCompanionCatalog(catalog)) {
    return;
  }

  const auto* const dbc_loader = session.GetDbcLoader();
  const auto* const spell_entry =
      dbc_loader != nullptr ? dbc_loader->spell().LookupEntry(spell_id) : nullptr;
  if (spell_entry == nullptr || spell_entry->effect_misc_value[0] <= 0) {
    return;
  }

  const auto creature_entry =
      static_cast<std::uint32_t>(spell_entry->effect_misc_value[0]);
  auto& system = SpellbookSystem::Get();
  const auto query_token = system.BeginCompanionNameQuery(spell_id);
  if (!query_token.has_value()) {
    return;
  }

  const auto callback_key = openwow::game::QueryCache::CallbackKey{
      reinterpret_cast<std::uintptr_t>(&FinishCompanionNameQuery), spell_id};
  const auto* creature = session.query_cache().GetOrRequestCreatureTemplate(
      creature_entry,
      openwow::game::QueryCache::QueryRequestOptions{
          .callback_key = callback_key,
          .dedupe_callbacks = true,
          .callback = [spell_id, creature_entry, token = *query_token,
                       show_learn_message, session_ptr = &session](
                          const bool success) {
            FinishCompanionNameQuery(*session_ptr, spell_id, creature_entry, token,
                                     show_learn_message, success);
          },
      });
  if (creature == nullptr) {
    return;
  }

  if (!system.CompleteCompanionNameQuery(spell_id, *query_token)) {
    return;
  }
  if (show_learn_message) {
    ui::game::DisplaySystemMessage(62, creature->name.c_str());
    FinalizeCompanionCatalogIfReady(session);
    FireCompanionCatalogMutationEvent(true);
  }
}

void RouteForgottenCompanion(const WorldSession& session,
                             const LearnedSpellCatalog catalog) {
  if (!IsCompanionCatalog(catalog)) {
    return;
  }
  FinalizeCompanionCatalogIfReady(session);
  FireCompanionCatalogMutationEvent(false);
}

void SyncLearnedTrackingSpell(const std::uint32_t spell_id) {
  const auto* const dbc_loader = SpellbookSystem::Get().GetDbcLoader();
  if (const auto entry =
          BuildLearnedTrackingSpellEntry(spell_id, dbc_loader);
      entry.has_value()) {
    TrackingSystem::Get().AddAvailableTracking(*entry);
  } else {
    TrackingSystem::Get().RemoveAvailableTrackingSpell(spell_id);
  }
}

bool SupportsLearnSpellAutoPlaceClass(const std::uint8_t class_id) {
  return class_id == kWarriorClassId || class_id == kRogueClassId ||
         class_id == kDruidClassId;
}

std::uint32_t FindFirstStanceFormId(const data::dbc::SpellEntry& spell) {
  return SpellShapeshiftMaskFirstFormId(
      MakeSpellShapeshiftMask(spell.stances, spell.stances_high));
}

std::uint32_t ToAutoPlaceActionSlotStart(
    const data::dbc::SpellShapeshiftFormEntry& form) {
  return kAutoPlaceBarSlotCount *
         (form.bonus_action_bar + kAutoPlaceActionBarOffsetBias);
}

std::optional<std::uint32_t> ResolveLearnSpellProgressionRank(
    const std::uint32_t spell_id,
    const data::dbc::SpellEntry* const spell_entry) {
  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {
    return ResolveSpellProgressionRank(*query);
  }

  if (spell_entry == nullptr) {
    return std::nullopt;
  }

  return ResolveSpellProgressionRank(spell_entry->rank, 0);
}

bool AutoPlaceLearnedSpellOnActionBar(WorldSession& session,
                                      const std::uint32_t spell_id,
                                      const std::uint32_t start_slot) {
  if (start_slot == 0) {
    return false;
  }

  const auto end_slot = start_slot + kAutoPlaceBarSlotCount;
  if (end_slot > ActionAssignmentRuntime::kMaxActionButtons) {
    return false;
  }

  for (std::uint32_t slot = start_slot; slot < end_slot; ++slot) {
    if (!session.action_assignments().GetPresentationEntry(slot).IsEmpty()) {
      continue;
    }

    ActionPresentationEntry button;
    button.action = spell_id;
    button.type = ActionPresentationKind::kSpell;
    session.action_assignments().SetPresentationEntry(slot, button);
    session.interaction().SendSetActionButton(static_cast<std::uint8_t>(slot),
                                              button);
    ui::game::ScriptEventDispatch::Get().FireActionbarSlotChanged(
        static_cast<std::uint8_t>(slot + 1));

    if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
      ui::game::ScriptEventDispatch::Get().FireActionbarUpdateUsable();
    }

    return true;
  }

  return false;
}

}

int CompareShapeshiftFormSpellOrder(
    const std::optional<ShapeshiftFormSpellOrderInfo>& info_a,
    const std::optional<ShapeshiftFormSpellOrderInfo>& info_b) {

  if (!info_a.has_value() || !info_b.has_value()) {
    return 0;
  }

  const auto order_a = info_a->order;
  const auto order_b = info_b->order;

  if (order_a < 0 && order_b < 0) {
    if (info_a->cached_id >= info_b->cached_id) {
      return 1;
    }
    return -1;
  }

  if (order_a == -1) {
    return 1;
  }

  if (order_b == -1) {
    return -1;
  }

  if (order_a == order_b) {
    return 0;
  }

  if (order_a >= order_b) {
    return 1;
  }
  return -1;
}

int CompareShapeshiftFormSpellOrder(
    const std::uint32_t spell_id_a,
    const std::uint32_t spell_id_b,
    const ShapeshiftFormSpellOrderLookup& lookup) {
  return CompareShapeshiftFormSpellOrder(lookup(spell_id_a), lookup(spell_id_b));
}

std::uint32_t SpellBookFrame::GetLearnSpellAutoPlaceActionSlotStart(
    const WorldSession& session,
    const std::uint32_t spell_id) {
  const auto* const dbc_loader = session.GetDbcLoader();
  const auto* const player = session.objects().GetLocalPlayerTyped();
  if (dbc_loader == nullptr || player == nullptr || spell_id == 0) {
    return 0;
  }

  const auto player_class = player->State().GetClass();
  if (!SupportsLearnSpellAutoPlaceClass(player_class)) {
    return 0;
  }

  const auto* const spell = dbc_loader->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  if ((spell->stances != 0 || spell->stances_high != 0) &&
      (spell->attributes_ex2 & kSpellAttr2SkipStanceAutoPlace) == 0) {
    const auto form_id = FindFirstStanceFormId(*spell);
    if (const auto* const form =
            dbc_loader->spell_shapeshift_form().LookupEntry(form_id);
        form != nullptr) {
      return ToAutoPlaceActionSlotStart(*form);
    }
    return 0;
  }

  if (player_class != kWarriorClassId) {
    return 0;
  }

  const auto current_form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
  if (current_form_id == 0) {
    return 0;
  }

  const auto* const form =
      dbc_loader->spell_shapeshift_form().LookupEntry(current_form_id);
  return form != nullptr ? ToAutoPlaceActionSlotStart(*form) : 0;
}

bool SpellBookFrame::IsUnitMatchingTarget(
    std::uint64_t unit_guid,
    std::uint64_t target_guid) {
  if (unit_guid == 0 || target_guid == 0) return false;

  if (unit_guid == target_guid) return true;

  return false;
}

bool SpellBookFrame::CanCastOnTarget(
    const WorldSession& session,
    std::uint32_t spell_id,
    std::uint64_t target_guid) {
  if (spell_id == 0) return false;

  auto& system = SpellbookSystem::Get();
  const bool is_visible_pet_spell = session.pet().HasSpellbookSpellId(spell_id);
  if (system.HasSpell(spell_id) || is_visible_pet_spell) {
    const auto spell = SpellQueryBridge::Get().Query(spell_id);
    return spell.has_value() &&
           Spell_HasSpellbookDirectCastEffect(spell->effectIds.data());
  }

  if (target_guid == 0) return false;

  return false;
}

void SpellBookFrame::DownrankSpellForTarget(
    const WorldSession& session,
    std::uint32_t* spell_id,
    const CGUnit_C* target_unit,
    const CGUnit_C* caster_unit) {
  if (!spell_id || *spell_id == 0 || !caster_unit) return;

  const auto* const objects = caster_unit->object_manager();
  if (objects == nullptr) return;

  const auto* const dbc = caster_unit->dbc_loader();
  if (dbc == nullptr) return;

  const auto* spell = dbc->spell().LookupEntry(*spell_id);
  if (spell == nullptr) return;

  if ((spell->attributes & kSpellAttr0NoDownrank) != 0) return;
  if ((spell->attributes_ex2 & kSpellAttrEx2DownrankSkip) != 0) return;

  bool has_effect = false;
  for (std::size_t i = 0; i < 3; ++i) {
    if (spell->effect[i] != 0) {
      has_effect = true;
      break;
    }
  }
  if (!has_effect) return;

  if (!target_unit) return;

  const bool caster_is_player = caster_unit->IsActivePlayer();
  if (!caster_is_player) {
    const ObjectGuid caster_guid = caster_unit->GetGuid();
    const auto* const active_player = objects->GetActivePlayer();
    const ObjectGuid pet_guid = active_player != nullptr
                                    ? active_player->State().GetPetGUID()
                                    : ObjectGuid{};
    if (pet_guid.IsEmpty() || caster_guid != pet_guid) return;
  }

  if (!caster_unit->Interaction().CanAssistSpellTarget(*target_unit, false)) return;

  std::int32_t target_level = static_cast<std::int32_t>(target_unit->State().GetLevel());
  {
    const ObjectGuid summoner_guid = target_unit->State().GetSummonedBy();
    if (!summoner_guid.IsEmpty()) {
      const auto* summoner = objects->GetUnit(summoner_guid);
      if (summoner != nullptr) {
        target_level = static_cast<std::int32_t>(summoner->State().GetLevel());
      }
    }
  }

  const std::int32_t threshold =
      target_level + kDownrankLevelBuffer;

  if (static_cast<std::int32_t>(spell->spell_level) <= threshold) return;

  const std::string_view spell_name = spell->spell_name;
  if (spell_name.empty()) return;

  std::int32_t best_level = 0;

  auto try_candidate = [&](std::uint32_t candidate_id) {
    const auto* candidate = dbc->spell().LookupEntry(candidate_id);
    if (candidate == nullptr) return;

    if (!StormUtf8NoCaseEquals(spell_name, candidate->spell_name)) return;

    const std::int32_t candidate_level =
        static_cast<std::int32_t>(candidate->spell_level);
    if (candidate_level <= threshold && candidate_level > best_level) {
      *spell_id = candidate_id;
      best_level = candidate_level;
    }
  };

  if (caster_is_player) {

    const auto& spell_list = SpellbookSystem::Get().GetSpellList();
    for (auto it = spell_list.rbegin(); it != spell_list.rend(); ++it) {
      try_candidate(it->spell_id);
    }
  } else {

    const auto& pet_spells = session.pet().pet_bar().spellbook_spells;
    for (auto it = pet_spells.rbegin(); it != pet_spells.rend(); ++it) {
      try_candidate(*it);
    }
  }
}

void SpellBookFrame::AddToSpellGroup(std::uint32_t spell_id,
                                     const bool from_pet_spellbook) {
  if (spell_id == 0) return;

  const auto spell = SpellQueryBridge::Get().Query(spell_id);
  if (!spell || spell->name.empty()) return;

  const std::uint32_t skill_level = ResolveSpellProgressionRank(*spell);

  SpellGroup* group = FindSpellGroup(s_spell_groups, spell->name);
  if (!group) {
    SpellGroup new_group;
    new_group.name = spell->name;
    new_group.name_hash = core::SStrHashCI(spell->name.c_str());
    s_spell_groups.push_back(std::move(new_group));
    group = &s_spell_groups.back();
  }

  for (const auto& entry : group->entries) {
    if (entry.spell_id == spell_id) return;
  }

  SpellGroupEntry new_entry;
  new_entry.spell_id = spell_id;
  new_entry.from_pet_spellbook = from_pet_spellbook;
  new_entry.subtext = spell->subtext;
  new_entry.skill_level = skill_level;

  if (from_pet_spellbook) {
    group->entries.push_back(std::move(new_entry));
  } else {
    auto it = group->entries.begin();
    while (it != group->entries.end()) {
      if (skill_level > it->skill_level) break;
      ++it;
    }
    group->entries.insert(it, std::move(new_entry));
  }

}

void SpellBookFrame::LearnSpell(WorldSession& session,
                                std::uint32_t spell_id,
                                bool show_learn_message,
                                std::uint32_t supersedes_id) {
  if (spell_id == 0) return;

  auto& system = SpellbookSystem::Get();
  auto& query_bridge = SpellQueryBridge::Get();
  const auto* const dbc_loader = system.GetDbcLoader();
  const auto* const spell_entry =
      dbc_loader != nullptr ? dbc_loader->spell().LookupEntry(spell_id) : nullptr;

  if (dbc_loader != nullptr && spell_entry == nullptr) {
    return;
  }
  const auto spell = query_bridge.Query(spell_id);
  const SpellInfo info = BuildSpellInfo(spell_id);
  const auto learned_catalog = ResolveLearnedSpellCatalog(system, spell_id);
  const auto superseded_catalog =
      supersedes_id != 0
          ? ResolveLearnedSpellCatalog(system, supersedes_id)
          : LearnedSpellCatalog::Spellbook;

  if (supersedes_id != 0 && supersedes_id != spell_id) {
    system.ForgetGatherInteractionSpell(supersedes_id);
    system.RemoveSpell(session.objects(), supersedes_id);
    RemoveSpellGroupEntry(s_spell_groups, supersedes_id, false);
    query_bridge.SetSpellKnownState(supersedes_id, false);
    TrackingSystem::Get().RemoveAvailableTrackingSpell(supersedes_id);
    if (auto* active_player = session.objects().GetActivePlayer();
        active_player != nullptr) {
      active_player->Casts().ForgetSpellCastTracking(*active_player, supersedes_id);
    }
    RouteForgottenCompanion(session, superseded_catalog);
    system.QueueUiEvent(SpellbookUiEventType::SpellsChanged);
    system.AddSpell(session.objects(), info);
  } else {
    system.AddSpell(session.objects(), info);
  }
  session.spellbook_private_usability().OnSpellLearned(
      session, spell_id, supersedes_id);

  query_bridge.SetSpellKnownState(spell_id, true);
  if (IsSpellNameLookupCatalog(learned_catalog)) {
    AddToSpellGroup(spell_id, false);
  }
  system.TrackLearnedGatherInteractionSpell(spell_id);
  SyncLearnedTrackingSpell(spell_id);
  RouteLearnedCompanion(session, spell_id, learned_catalog, show_learn_message);

  if (show_learn_message &&
      learned_catalog == LearnedSpellCatalog::Spellbook) {
    system.QueueUiEvent(SpellbookUiEventType::SpellsChanged);
    system.QueueLearnedSpellInTab(spell_id);
  }

  if (!show_learn_message) {
    return;
  }

  {
    const auto* const player = session.objects().GetLocalPlayerTyped();
    const auto spell_rank = ResolveLearnSpellProgressionRank(spell_id, spell_entry);

    if (learned_catalog == LearnedSpellCatalog::Spellbook &&
        player != nullptr && spell_entry != nullptr && spell_rank.has_value() &&
        player->State().GetLevel() <= 10 && *spell_rank <= 1 &&
        (spell_entry->attributes & kSpellAttr0Passive) == 0 &&
        (spell_entry->attributes_ex & kSpellAttr1LearnSpellAutoPlaceSuppress) == 0 &&
        !Spell_HasAuraType(spell_entry->effect_apply_aura.data(),
                           kShapeshiftAuraType)) {
      AutoPlaceLearnedSpellOnActionBar(
          session, spell_id,
          GetLearnSpellAutoPlaceActionSlotStart(session, spell_id));
    }
  }

  const std::string_view learned_name =
      spell_entry != nullptr && !spell_entry->spell_name.empty()
          ? spell_entry->spell_name
          : spell.has_value() ? std::string_view{spell->name}
                              : std::string_view{};
  const std::string_view learned_rank =
      spell_entry != nullptr ? spell_entry->rank
                             : spell.has_value() ? std::string_view{spell->subtext}
                                                 : std::string_view{};
  if (!learned_name.empty() &&
      learned_catalog == LearnedSpellCatalog::TradeSkill) {
    ui::game::DisplaySystemMessage(61, learned_name.data());
  } else if (!learned_name.empty() &&
             (learned_catalog == LearnedSpellCatalog::Spellbook ||
              learned_catalog == LearnedSpellCatalog::HiddenSpellbook)) {
    const std::string learned_text =
        FormatLearnedSpellText(learned_name, learned_rank);
    const bool is_ability =
        spell_entry != nullptr
            ? (spell_entry->attributes & kSpellAttr0Ability) != 0
            : spell.has_value() &&
                  (spell->attributes & kSpellAttr0Ability) != 0;
    ui::game::DisplaySystemMessage(is_ability ? 60 : 59,
                                   learned_text.c_str());
  }
}

void SpellBookFrame::FinalizeInitialCompanionCatalog(WorldSession& session) {
  FinalizeCompanionCatalogIfReady(session);
}

std::uint32_t SpellBookFrame::MultiCastTotemCategoryToSlotMask(
    const std::uint32_t totem_category) {
  switch (totem_category) {
    case 2:
      return 0x2U;
    case 3:
      return 0x8U;
    case 4:
      return 0x1U;
    case 5:
      return 0x4U;
    case 21:
      return 0xFU;
    default:
      return 0;
  }
}

std::vector<std::uint32_t> SpellBookFrame::GetMultiCastTotemSpells(
    const std::uint8_t slot_index,
    const ::openwow::data::dbc::DbcLoader* dbc_loader) {
  if (slot_index >= 4 || dbc_loader == nullptr) {
    return {};
  }

  std::vector<MultiCastTotemSpellChoice> choices;
  for (const auto& spell_info : SpellbookSystem::Get().GetKnownSpellList()) {
    if (spell_info.spell_id == 0) {
      continue;
    }

    const auto* spell = dbc_loader->spell().LookupEntry(spell_info.spell_id);
    if (spell == nullptr || !SpellMatchesMultiCastTotemSlot(*spell, slot_index)) {
      continue;
    }

    if (std::find_if(choices.begin(), choices.end(),
                     [&](const MultiCastTotemSpellChoice& choice) {
                       return choice.spell_id == spell_info.spell_id;
                     }) != choices.end()) {
      continue;
    }

    const auto spell_name = std::string_view(spell->spell_name);
    const auto sort_key =
        ResolveSpellProgressionRank(spell->rank, spell->spell_level);
    const auto existing = std::find_if(
        choices.begin(), choices.end(),
        [&](const MultiCastTotemSpellChoice& choice) {
          return StormUtf8NoCaseEquals(choice.name, spell_name);
        });
    if (existing != choices.end()) {
      if (existing->sort_key < sort_key) {
        existing->spell_id = spell_info.spell_id;
        existing->sort_key = sort_key;
      }
      continue;
    }

    choices.push_back(MultiCastTotemSpellChoice{
        spell_info.spell_id,
        sort_key,
        std::string(spell_name),
    });
  }

  std::vector<std::uint32_t> spell_ids;
  spell_ids.reserve(choices.size());
  for (const auto& choice : choices) {
    spell_ids.push_back(choice.spell_id);
  }
  return spell_ids;
}

bool SpellBookFrame::SyncAutoFilledMultiCastSlots(WorldSession& session) {
  const auto* dbc_loader = session.GetDbcLoader();
  if (dbc_loader == nullptr) {
    return false;
  }

  constexpr std::size_t kFirstMultiCastActionSlot = 132;
  constexpr std::size_t kMultiCastSlotCount = 4;
  constexpr std::size_t kMultiCastActionRows = 3;

  auto& cvars = ui::game::CVarSystem::Instance();
  const auto original_mask =
      static_cast<std::uint32_t>(cvars.GetCVarInt("autoFilledMultiCastSlots"));
  std::uint32_t updated_mask = original_mask;
  bool changed = false;

  for (std::size_t slot_index = 0; slot_index < kMultiCastSlotCount; ++slot_index) {
    const auto slot_spells =
        GetMultiCastTotemSpells(static_cast<std::uint8_t>(slot_index), dbc_loader);
    const auto slot_bit = 1U << slot_index;

    if ((original_mask & slot_bit) != 0) {
      if (slot_spells.empty()) {
        updated_mask &= ~slot_bit;
      }
      continue;
    }

    if (slot_spells.empty()) {
      continue;
    }

    const auto spell_id = slot_spells.front();
    for (std::size_t row = 0; row < kMultiCastActionRows; ++row) {
      const auto action_slot = kFirstMultiCastActionSlot + slot_index + (row * kMultiCastSlotCount);
      if (!session.action_assignments().GetPresentationEntry(action_slot).IsEmpty()) {
        continue;
      }

      ActionPresentationEntry button;
      button.action = spell_id;
      button.type = ActionPresentationKind::kSpell;
      session.action_assignments().SetPresentationEntry(action_slot, button);
      session.interaction().SendSetActionButton(
          static_cast<std::uint8_t>(action_slot), button);
      ui::game::ScriptEventDispatch::Get().FireActionbarSlotChanged(
          static_cast<std::uint8_t>(action_slot + 1));
      changed = true;
    }

    updated_mask |= slot_bit;
  }

  if (updated_mask != original_mask) {
    (void)cvars.SetCVar("autoFilledMultiCastSlots",
                        std::to_string(updated_mask), true);
    changed = true;
  }

  if (changed && ui::game::detail::RefreshAllActionSlotValidation(session)) {
    ui::game::ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }

  return changed;
}

bool SpellBookFrame::HandleCarriedMultiCastTotemCategory(
    WorldSession& session,
    const std::uint32_t totem_category) {
  const auto slot_mask = MultiCastTotemCategoryToSlotMask(totem_category);
  if (slot_mask == 0) {
    return false;
  }

  if (session.objects().GetActivePlayer() != nullptr) {
    const auto* dbc_loader = session.GetDbcLoader();
    auto& tutorials = TutorialSystem::Instance();
    if (dbc_loader != nullptr && tutorials.IsSeenBitsInitialized() &&
        !tutorials.IsTutorialSeen(46)) {
      for (std::uint8_t slot_index = 0; slot_index < 4; ++slot_index) {
        if ((slot_mask & (1U << slot_index)) == 0) {
          continue;
        }
        if (!GetMultiCastTotemSpells(slot_index, dbc_loader).empty()) {
          tutorials.TriggerTutorial(46);
          break;
        }
      }
    }
  }

  (void)SyncAutoFilledMultiCastSlots(session);
  return true;
}

bool SpellBookFrame::HandleLearnedMultiCastTotemSlotMask(
    WorldSession& session,
    const std::uint32_t slot_mask) {
  if (slot_mask == 0) {
    return false;
  }

  (void)SyncAutoFilledMultiCastSlots(session);
  return true;
}

bool SpellBookFrame::HandleTrackedMultiCastTotemItemEntry(
    WorldSession& session,
    const std::uint32_t item_entry) {
  if (!IsTrackedCarriedItemEntry(session.inventory_replica(), item_entry)) {
    return false;
  }

  const auto* item_template =
      session.query_cache().GetOrRequestItemTemplate(item_entry);
  if (item_template == nullptr) {
    return false;
  }

  return HandleCarriedMultiCastTotemCategory(session,
                                              item_template->totem_category);
}

void SpellBookFrame::ForgetSpell(WorldSession& session,
                                 std::uint32_t spell_id) {
  if (spell_id == 0) {
    return;
  }

  auto& system = SpellbookSystem::Get();
  const auto removed_catalog = ResolveLearnedSpellCatalog(system, spell_id);
  system.RemoveSpell(session.objects(), spell_id);
  session.spellbook_private_usability().OnSpellForgotten(session, spell_id);
  system.ForgetGatherInteractionSpell(spell_id);
  TrackingSystem::Get().RemoveAvailableTrackingSpell(spell_id);
  RemoveSpellGroupEntry(s_spell_groups, spell_id, false);
  SpellQueryBridge::Get().SetSpellKnownState(spell_id, false);
  if (auto* active_player = session.objects().GetActivePlayer();
      active_player != nullptr) {
    active_player->Casts().ForgetSpellCastTracking(*active_player, spell_id);
  }
  RouteForgottenCompanion(session, removed_catalog);
  system.QueueUiEvent(SpellbookUiEventType::SpellsChanged);
}

std::optional<ResolvedSpellGroupEntry> SpellBookFrame::ResolveSpellByName(
    const std::string_view spell_name,
    const std::string_view qualifier) {
  if (spell_name.empty()) {
    return std::nullopt;
  }

  const SpellGroup* group = FindSpellGroup(s_spell_groups, spell_name);
  if (!group) {
    return std::nullopt;
  }

  if (qualifier.empty()) {
    if (group->entries.empty()) {
      return std::nullopt;
    }

    const auto& entry = group->entries.front();
    return ResolvedSpellGroupEntry{
        entry.spell_id,
        entry.from_pet_spellbook,
    };
  }

  for (const auto& entry : group->entries) {
    if (!StormUtf8NoCaseEquals(entry.subtext, qualifier)) {
      continue;
    }

    return ResolvedSpellGroupEntry{
        entry.spell_id,
        entry.from_pet_spellbook,
    };
  }

  return std::nullopt;
}

void SpellBookFrame::RebuildPetSpellGroups(
    const std::vector<std::uint32_t>& spell_ids) {
  RemoveSpellGroupEntries(
      s_spell_groups,
      [](const SpellGroupEntry& entry) {
        return entry.from_pet_spellbook;
      });

  for (const auto spell_id : spell_ids) {
    AddToSpellGroup(spell_id, true);
  }
}

const std::vector<SpellGroup>& SpellBookFrame::GetSpellGroups() {
  return s_spell_groups;
}

void SpellBookFrame::ClearSpellGroups() {
  s_spell_groups.clear();
}

std::int32_t SpellBook_ResolveSpellSlotByNameQuery(
    const WorldSession& session,
    std::string_view spell_name_query,
    bool& out_is_pet_book) {

  if (spell_name_query.empty()) {
    return -1;
  }

  std::string raw_name(spell_name_query);

  if (!raw_name.empty() && raw_name.front() == '!') {
    raw_name.erase(raw_name.begin());
  }

  if (raw_name.empty()) {
    return -1;
  }

  auto match = SpellBookFrame::ResolveSpellByName(raw_name, {});

  if (!match.has_value()) {

    const auto open_paren = raw_name.find('(');
    std::string qualifier;

    if (open_paren != std::string::npos) {
      qualifier = raw_name.substr(open_paren + 1);
      raw_name.resize(open_paren);

      if (const auto close_paren = qualifier.find(')');
          close_paren != std::string::npos) {
        qualifier.resize(close_paren);
      }
    }

    match = SpellBookFrame::ResolveSpellByName(raw_name, qualifier);
  }

  if (!match.has_value()) {
    return -1;
  }

  const std::uint32_t spell_id = match->spell_id;
  out_is_pet_book = match->from_pet_spellbook;

  if (spell_id == 0) {
    return -1;
  }

  if (out_is_pet_book) {
    for (std::size_t slot = session.pet().GetSpellbookSpellCount();
         slot > 0; --slot) {
      if (session.pet().GetSpellbookSpellId(slot) == spell_id) {
        return static_cast<std::int32_t>(slot - 1);
      }
    }

    return -1;
  }

  const auto& spellbook = SpellbookSystem::Get();
  for (std::size_t slot = spellbook.GetPlayerSpellBookSlotCount();
       slot > 0; --slot) {
    const auto* spell =
        spellbook.GetPlayerSpellBookSlot(static_cast<std::uint32_t>(slot));
    if (spell != nullptr && spell->spell_id == spell_id) {
      return static_cast<std::int32_t>(slot - 1);
    }
  }

  return -1;
}

}

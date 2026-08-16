
#include "openwow/ui/game/api/game_lua_api_companion.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include "openwow/game/aura_tracker.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/spellbook_catalog.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/template_name_variant.h"
#include "openwow/ui/lua_numeric.h"

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::ui::game::detail {

static int s_active_critter_spell = 0;
static int s_active_mount_spell = 0;

namespace {

constexpr std::uint32_t kSpellEffectSummon = 28;

enum class CompanionCollectionType {
  kCritter = 0,
  kMount = 1,
};

enum class CompanionSpellKind {
  kNone,
  kCritter,
  kMount,
};

bool IsCritterCompanionSpell(const openwow::data::dbc::SpellEntry &spell,
                             const openwow::data::dbc::DbcLoader &dbc) {
  return openwow::game::ClassifyLearnedSpell(spell, dbc) ==
         openwow::game::LearnedSpellCatalog::Critter;
}

bool IsCompanionSpell(const openwow::data::dbc::SpellEntry &spell,
                      const openwow::data::dbc::DbcLoader &dbc) {
  const auto catalog = openwow::game::ClassifyLearnedSpell(spell, dbc);
  return catalog == openwow::game::LearnedSpellCatalog::Critter ||
         catalog == openwow::game::LearnedSpellCatalog::Mount;
}

CompanionSpellKind ResolveCompanionSpellKind(
    const openwow::data::dbc::SpellEntry &spell,
    const openwow::data::dbc::DbcLoader &dbc) {
  const auto catalog = openwow::game::ClassifyLearnedSpell(spell, dbc);
  if (catalog == openwow::game::LearnedSpellCatalog::Critter) {
    return CompanionSpellKind::kCritter;
  }
  if (catalog == openwow::game::LearnedSpellCatalog::Mount) {
    return CompanionSpellKind::kMount;
  }

  return CompanionSpellKind::kNone;
}

std::uint32_t FindCompanionSpellIndex(const std::vector<uint32_t> &spells,
                                      const std::uint32_t spell_id) {
  for (std::size_t index = spells.size(); index > 0; --index) {
    if (spells[index - 1] == spell_id) {
      return static_cast<std::uint32_t>(index);
    }
  }

  return 0;
}

bool TryParseCompanionCollectionType(const char *type, CompanionCollectionType &out_type) {
  if (type == nullptr) {
    return false;
  }

  if (std::strcmp(type, "CRITTER") == 0) {
    out_type = CompanionCollectionType::kCritter;
    return true;
  }

  if (std::strcmp(type, "MOUNT") == 0) {
    out_type = CompanionCollectionType::kMount;
    return true;
  }

  return false;
}

openwow::game::CompanionSpellType ToCompanionSpellType(
    const CompanionCollectionType type) {
  return type == CompanionCollectionType::kMount
             ? openwow::game::CompanionSpellType::Mount
             : openwow::game::CompanionSpellType::Critter;
}

std::vector<std::uint32_t> GetCompanionSpellList(
    const CompanionCollectionType type) {
  return openwow::game::SpellbookSystem::Get().GetCompanionSpellList(
      ToCompanionSpellType(type));
}

std::uint32_t GetActiveCompanionSpellCache(CompanionCollectionType type) {
  return type == CompanionCollectionType::kMount
             ? static_cast<std::uint32_t>(s_active_mount_spell)
             : static_cast<std::uint32_t>(s_active_critter_spell);
}

void SetActiveCompanionSpellCache(CompanionCollectionType type, std::uint32_t spell_id) {
  if (type == CompanionCollectionType::kMount) {
    s_active_mount_spell = static_cast<int>(spell_id);
    return;
  }

  s_active_critter_spell = static_cast<int>(spell_id);
}

bool ResolveCompanionSpellByIndex(CompanionCollectionType type, std::uint32_t zero_based_index,
                                  std::uint32_t &out_spell_id) {
  const auto spells = GetCompanionSpellList(type);
  if (zero_based_index >= spells.size()) {
    return false;
  }

  out_spell_id = spells[zero_based_index];
  return out_spell_id != 0;
}

bool TrySelectRandomCritterSpellId(openwow::game::WorldSession& session,
                                   std::uint32_t& out_spell_id) {
  const auto spells = GetCompanionSpellList(CompanionCollectionType::kCritter);
  if (spells.empty()) {
    return false;
  }

  const auto selected_index =
      static_cast<std::size_t>(session.client_random().Next()) % spells.size();
  out_spell_id = spells[selected_index];
  return true;
}

struct CompanionIndexParse {
  bool usage_error = false;
  std::optional<std::uint32_t> zero_based_index;
};

CompanionIndexParse ReadCompanionZeroBasedIndex(lua_State *L, int index) {
  const double raw_index = lua_tonumber(L, index);
  if (raw_index == 0.0) {
    CompanionIndexParse result;
    result.usage_error = true;
    return result;
  }

  CompanionIndexParse result;
  result.zero_based_index = openwow::ui::SaturateLuaNumberToU32(raw_index) - 1u;
  return result;
}

CompanionCollectionType ReadCompanionTypeOrRaise(lua_State *L, const char *usage_message) {
  CompanionCollectionType type = CompanionCollectionType::kCritter;
  if (!TryParseCompanionCollectionType(lua_tostring(L, 1), type)) {
    luaL_error(L, usage_message);
  }
  return type;
}

std::uint32_t ResolveObservedActiveMountSpellId(const openwow::game::WorldSession &session) {
  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return 0;
  }

  const auto player_guid = player->GetGuid();
  if (player_guid.IsEmpty()) {
    return 0;
  }

  auto &aura_tracker = openwow::game::AuraTracker::Get();
  for (const auto spell_id :
       GetCompanionSpellList(CompanionCollectionType::kMount)) {
    if (spell_id != 0 && aura_tracker.HasAura(player_guid, spell_id)) {
      return spell_id;
    }
  }

  return 0;
}

bool SpellMatchesCritterEntry(const openwow::data::dbc::SpellEntry &spell,
                              const openwow::data::dbc::DbcLoader &dbc,
                              std::uint32_t critter_entry) {
  if (critter_entry == 0) {
    return false;
  }

  if (!IsCritterCompanionSpell(spell, dbc)) {
    return false;
  }

  for (std::size_t effect_index = 0; effect_index < spell.effect.size(); ++effect_index) {
    if (spell.effect[effect_index] != kSpellEffectSummon) {
      continue;
    }
    if (spell.effect_misc_value[effect_index] <= 0) {
      continue;
    }
    if (static_cast<std::uint32_t>(spell.effect_misc_value[effect_index]) == critter_entry) {
      return true;
    }
  }

  return false;
}

std::int32_t ResolveCompanionCreatureId(const openwow::data::dbc::SpellEntry &spell) {
  return spell.effect_misc_value[0];
}

std::optional<std::string> ResolveCompanionIconPath(const openwow::data::dbc::DbcLoader &dbc,
                                                    std::uint32_t icon_id) {
  if (icon_id == 0) {
    return std::nullopt;
  }

  const auto *icon = dbc.spell_icon().LookupEntry(icon_id);
  if (icon == nullptr || std::string_view(icon->icon_path).empty()) {
    return std::nullopt;
  }

  return std::string(icon->icon_path);
}

bool IsCompanionSpellObservedActive(const openwow::game::WorldSession &session,
                                    const openwow::data::dbc::SpellEntry &spell,
                                    const openwow::data::dbc::DbcLoader &dbc) {
  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return false;
  }

  return openwow::game::IsSpellRecordCurrentForUnit(spell, dbc, player);
}

std::uint32_t FindCritterSpellInActionBar(const openwow::game::WorldSession &session,
                                          std::uint32_t critter_entry) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || critter_entry == 0) {
    return 0;
  }

  for (std::size_t slot_index = 0; slot_index < openwow::game::ActionAssignmentRuntime::kMaxActionButtons;
       ++slot_index) {
    const auto &button = session.action_assignments().GetPresentationEntry(slot_index);
    if (button.IsEmpty()) {
      continue;
    }

    const auto spell_id = ResolveSpellLikeActionIdForValidation(const_cast<openwow::game::WorldSession&>(session), button, slot_index);
    if (spell_id == 0) {
      continue;
    }

    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell != nullptr && SpellMatchesCritterEntry(*spell, *dbc, critter_entry)) {
      return spell_id;
    }
  }

  return 0;
}

std::uint32_t ResolveActiveCritterSpellId(const openwow::game::WorldSession &session,
                                          std::uint32_t critter_entry) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || critter_entry == 0) {
    return 0;
  }

  for (const auto spell_id :
       GetCompanionSpellList(CompanionCollectionType::kCritter)) {
    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell != nullptr && SpellMatchesCritterEntry(*spell, *dbc, critter_entry)) {
      return spell_id;
    }
  }

  return FindCritterSpellInActionBar(session, critter_entry);
}

std::uint32_t ResolveObservedActiveCompanionSpellId(
    CompanionCollectionType type, const openwow::game::WorldSession* session) {
  if (session != nullptr) {
    const auto observed_spell_id =
        type == CompanionCollectionType::kMount
            ? ResolveObservedActiveMountSpellId(*session)
            : ResolveActiveCritterSpellId(*session, session->objects().GetActivePlayer() == nullptr
                                                        ? 0U
                                                        : session->objects()
                                                              .GetActivePlayer()
                                                              ->GetGuidField(UNIT_FIELD_CRITTER)
                                                              .GetEntry());
    if (observed_spell_id != 0 || GetActiveCompanionSpellCache(type) == 0) {
      return observed_spell_id;
    }
  }

  return GetActiveCompanionSpellCache(type);
}

void RefreshCritterActionBarSlotsForEntry(openwow::game::WorldSession &session,
                                          std::uint32_t critter_entry) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || critter_entry == 0) {
    return;
  }

  auto& assignments = session.action_assignments();
  auto& dispatch = ScriptEventDispatch::Get();

  for (std::size_t slot_index = 0; slot_index < openwow::game::ActionAssignmentRuntime::kMaxActionButtons;
       ++slot_index) {
    const auto& button = assignments.GetPresentationEntry(slot_index);
    if (button.IsEmpty()) {
      continue;
    }

    const auto spell_id = ResolveSpellLikeActionIdForValidation(session, button, slot_index);
    if (spell_id == 0) {
      continue;
    }

    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell == nullptr || !SpellMatchesCritterEntry(*spell, *dbc, critter_entry)) {
      continue;
    }

    (void)assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
    dispatch.FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
  }
}

}

bool ResolveCompanionSpellCursorInfo(lua_State *L, std::uint32_t spell_id, std::uint32_t &out_index,
                                     const char *&out_type) {
  out_index = 0;
  out_type = nullptr;

  const auto *dbc = GetDbcLoader(L);
  const auto *spell = LookupSpellEntry(L, spell_id);
  if (dbc == nullptr || spell == nullptr || !IsCompanionSpell(*spell, *dbc)) {
    return false;
  }

  switch (ResolveCompanionSpellKind(*spell, *dbc)) {
  case CompanionSpellKind::kCritter:
    out_index = FindCompanionSpellIndex(
        GetCompanionSpellList(CompanionCollectionType::kCritter), spell_id);
    out_type = "CRITTER";
    return true;
  case CompanionSpellKind::kMount:
    out_index = FindCompanionSpellIndex(
        GetCompanionSpellList(CompanionCollectionType::kMount), spell_id);
    out_type = "MOUNT";
    return true;
  case CompanionSpellKind::kNone:
    return false;
  }

  return false;
}

bool GetCompanionSpellByTypeAndIndex(const char *type, std::uint32_t zero_based_index,
                                     std::uint32_t &out_spell_id) {
  CompanionCollectionType collection_type = CompanionCollectionType::kCritter;
  if (!TryParseCompanionCollectionType(type, collection_type)) {
    return false;
  }

  return ResolveCompanionSpellByIndex(collection_type, zero_based_index, out_spell_id);
}

void SetActiveCompanionSpellForType(const char *type, std::uint32_t spell_id) {
  CompanionCollectionType collection_type = CompanionCollectionType::kCritter;
  if (!TryParseCompanionCollectionType(type, collection_type)) {
    collection_type = CompanionCollectionType::kCritter;
  }

  SetActiveCompanionSpellCache(collection_type, spell_id);
}

void ClearActiveCompanionSpellForType(const char *type) {
  SetActiveCompanionSpellForType(type, 0);
}

bool IsCompanionSpellActive(std::uint32_t spell_id) {
  const auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  const auto* const session = manager != nullptr ? manager->world_session() : nullptr;
  return spell_id != 0 &&
         (ResolveObservedActiveCompanionSpellId(CompanionCollectionType::kCritter, session) ==
              spell_id ||
          ResolveObservedActiveCompanionSpellId(CompanionCollectionType::kMount, session) ==
              spell_id);
}

void SetCompanionSpellListForTesting(const char *type, std::vector<std::uint32_t> spell_ids) {
  CompanionCollectionType collection_type = CompanionCollectionType::kCritter;
  if (!TryParseCompanionCollectionType(type, collection_type)) {
    return;
  }

  openwow::game::SpellbookSystem::Get().SetCompanionSpellListOrder(
      ToCompanionSpellType(collection_type), std::move(spell_ids));
}

void ResetCompanionApiStateForTesting() {
  openwow::game::SpellbookSystem::Get().ClearCompanionSpellLists();
  s_active_critter_spell = 0;
  s_active_mount_spell = 0;
}

void HandleCritterCompanionEntryChanged(openwow::game::WorldSession &session,
                                        std::uint32_t current_entry, std::uint32_t previous_entry) {
  SetActiveCompanionSpellForType("CRITTER", ResolveActiveCritterSpellId(session, current_entry));
  RefreshCritterActionBarForDescriptorChange(session, previous_entry, current_entry);
}

void RefreshCritterActionBarForDescriptorChange(
    openwow::game::WorldSession &session, const std::uint32_t previous_entry,
    const std::uint32_t current_entry) {

  RefreshCritterActionBarSlotsForEntry(session, previous_entry);
  RefreshCritterActionBarSlotsForEntry(session, current_entry);
}

int LuaGetNumCompanions(lua_State *L) {
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: GetNumCompanions(type)");
  lua_pushnumber(L, static_cast<lua_Number>(GetCompanionSpellList(type).size()));
  return 1;
}

int LuaGetCompanionInfo(lua_State *L) {
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: GetCompanionInfo(type, index)");
  const auto parsed_index = ReadCompanionZeroBasedIndex(L, 2);
  if (parsed_index.usage_error) {
    luaL_error(L, "Usage: GetCompanionInfo(type, index)");
  }
  if (!parsed_index.zero_based_index.has_value()) {
    return 0;
  }

  std::uint32_t spell_id = 0;
  if (!ResolveCompanionSpellByIndex(type, *parsed_index.zero_based_index, spell_id)) {
    return 0;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  const auto *session = GetWorldSession(L);
  const auto creature_id = ResolveCompanionCreatureId(*spell);
  const bool is_summoned =
      session != nullptr && IsCompanionSpellObservedActive(*session, *spell, *dbc);

  lua_pushnumber(L, static_cast<lua_Number>(creature_id));

  if (session != nullptr && creature_id > 0) {
    const auto *creature = const_cast<openwow::game::QueryCache&>(session->query_cache()).GetOrRequestCreatureTemplate(
        static_cast<std::uint32_t>(creature_id));
    if (creature != nullptr) {
      const auto creature_name = openwow::game::GetTemplateNameVariantOrBase(
          std::string_view(creature->name), creature->alternate_names, 0);
      lua_pushstring(L, std::string(creature_name).c_str());
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(spell_id));

  const auto icon_id =
      is_summoned && spell->active_icon_id != 0 ? spell->active_icon_id : spell->spell_icon_id;
  if (const auto icon_path = ResolveCompanionIconPath(*dbc, icon_id); icon_path.has_value()) {
    lua_pushstring(L, icon_path->c_str());
  } else {
    lua_pushnil(L);
  }

  if (is_summoned) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 5;
}

int LuaGetCompanionCooldown(lua_State *L) {
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: GetCompanionCooldown(type, index)");
  const auto parsed_index = ReadCompanionZeroBasedIndex(L, 2);
  if (parsed_index.usage_error) {
    return luaL_error(L, "Usage: GetCompanionCooldown(type, index)");
  }

  auto *session = GetWorldSession(L);
  if (!session || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  if (!parsed_index.zero_based_index.has_value()) {
    return 0;
  }
  std::uint32_t spell_id = 0;
  if (!ResolveCompanionSpellByIndex(type, *parsed_index.zero_based_index, spell_id)) {
    return 0;
  }

  if (const auto cooldown =
          openwow::game::ResolveSpellbookCooldown(session->spell_book(), spell_id);
      cooldown.has_value()) {
    lua_pushnumber(L, cooldown->start_time_s);
    lua_pushnumber(L, cooldown->duration_s);
    lua_pushnumber(L, cooldown->enabled);
    return 3;
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 1);
  return 3;
}

int LuaCallCompanion(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: CallCompanion(type, index)");
  const auto parsed_index = ReadCompanionZeroBasedIndex(L, 2);
  if (parsed_index.usage_error) {
    luaL_error(L, "Usage: CallCompanion(type, index)");
  }
  if (!session || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  std::uint32_t spell_id = 0;
  if (!parsed_index.zero_based_index.has_value() ||
      !ResolveCompanionSpellByIndex(type, *parsed_index.zero_based_index, spell_id)) {
    return 0;
  }

  session->interaction().SendCastSpell(spell_id, 0, 0);
  return 0;
}

int LuaDismissCompanion(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: DismissCompanion(type)");
  auto *const active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (!session || active_player == nullptr) {
    return 0;
  }

  const auto *dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  bool any_active_companion = false;
  for (const auto spell_id : GetCompanionSpellList(type)) {
    if (spell_id == 0) {
      continue;
    }

    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell == nullptr || !IsCompanionSpellObservedActive(*session, *spell, *dbc)) {
      continue;
    }

    any_active_companion = true;
    if (type != CompanionCollectionType::kCritter) {
      session->interaction().SendCastSpell(spell_id, 0, 0);
    }
  }

  if (any_active_companion && type == CompanionCollectionType::kCritter) {
    const auto critter_guid = active_player->State().GetCritterGUID();
    if (!critter_guid.IsEmpty()) {
      session->interaction().SendDismissCritter(critter_guid.GetRawValue());
    }
  }
  return 0;
}

int LuaSummonRandomCritter(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 1;
  }

  std::uint32_t spell_id = 0;
  if (!TrySelectRandomCritterSpellId(*session, spell_id)) {
    return 1;
  }

  session->interaction().SendCastSpell(spell_id, 0, 0);
  return 1;
}

int LuaGetNumStablePets(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  std::size_t stable_pet_count = 0;
  for (const auto &pet : session->pet().stable_list().pets) {
    if ((pet.flags & 0x2u) != 0) {
      ++stable_pet_count;
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(stable_pet_count));
  return 1;
}

int LuaPickupCompanion(lua_State *L) {
  const auto type = ReadCompanionTypeOrRaise(L, "Usage: PickupCompanion(type, index)");
  const auto parsed_index = ReadCompanionZeroBasedIndex(L, 2);
  if (parsed_index.usage_error) {
    return luaL_error(L, "Usage: PickupCompanion(type, index)");
  }

  if (!GameUI_CanPerformProtectedAction(
          protected_action_kind::kActionSlotMutation)) {
    return 0;
  }

  std::uint32_t spell_id = 0;
  if (!parsed_index.zero_based_index.has_value() ||
      !ResolveCompanionSpellByIndex(type, *parsed_index.zero_based_index, spell_id)) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  auto* cursor = session != nullptr ? session->held_cursor() : nullptr;
  if (cursor == nullptr) {
    return 0;
  }
  const auto* held_spell =
      cursor->get_if<::openwow::game::actions::held_cursor::Spell>();
  const auto held_spell_id =
      held_spell != nullptr ? held_spell->spell_id : 0u;
  cursor->Clear();

  if (held_spell_id == spell_id) {
    if (spell_id != 0) {
      cursor->PlaySound(
          ::openwow::game::actions::held_cursor::Sound::SpellbookDrop);
    }
    return 0;
  }

  if (static_cast<std::int32_t>(held_spell_id) > 0) {
    return 0;
  }

  namespace held_cursor = ::openwow::game::actions::held_cursor;
  cursor->HoldSpell(
      held_cursor::Spell{.spell_id = spell_id},
      held_cursor::Presentation{
          .texture_path =
              cursor_texture::ResolveSpellTexturePath(L, spell_id),
          .sound = held_cursor::Sound::SpellbookPickup,
          .grid = held_cursor::Grid::ActionBar,
      });
  return 0;
}

}

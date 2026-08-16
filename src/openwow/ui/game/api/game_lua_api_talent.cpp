
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_helpers.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "openwow/game/interaction_sender.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

namespace openwow::ui::game::detail {

static const openwow::data::dbc::DbcLoader* GetDbc(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return nullptr;

  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto* dbc = static_cast<const openwow::data::dbc::DbcLoader*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

static int ParseBoolStringOrDefault(const char* text, int default_value) {
  if (!text || text[0] == '\0') {
    return default_value;
  }

  switch (text[0]) {
    case '0':
    case 'F':
    case 'N':
    case 'f':
    case 'n':
      return 0;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'T':
    case 'Y':
    case 't':
    case 'y':
      return 1;
    default:
      break;
  }

  auto equals_ignore_case = [](const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
      return false;
    }

    while (*lhs != '\0' && *rhs != '\0') {
      const char lhs_char = (*lhs >= 'A' && *lhs <= 'Z')
                                ? static_cast<char>(*lhs - 'A' + 'a')
                                : *lhs;
      const char rhs_char = (*rhs >= 'A' && *rhs <= 'Z')
                                ? static_cast<char>(*rhs - 'A' + 'a')
                                : *rhs;
      if (lhs_char != rhs_char) {
        return false;
      }
      ++lhs;
      ++rhs;
    }

    return *lhs == '\0' && *rhs == '\0';
  };

  if (equals_ignore_case(text, "off") ||
      equals_ignore_case(text, "disabled")) {
    return 0;
  }
  if (equals_ignore_case(text, "on") ||
      equals_ignore_case(text, "enabled")) {
    return 1;
  }
  return default_value;
}

static bool ReadBoolArgOrDefault(lua_State* L, int index, bool default_value) {
  switch (lua_type(L, index)) {
    case LUA_TNIL:
    case LUA_TNONE:
      return false;
    case LUA_TBOOLEAN:
      return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
      return lua_tonumber(L, index) != 0.0;
    case LUA_TSTRING:
      return ParseBoolStringOrDefault(lua_tostring(L, index),
                                      default_value ? 1 : 0) != 0;
    default:
      return default_value;
  }
}

static std::uint32_t ResolveTalentGroupIndexArg(lua_State* L, int index) {
  std::optional<std::uint32_t> group_arg;
  if (lua_isnumber(L, index)) {
    group_arg = static_cast<std::uint32_t>(
        TruncateLuaNumberToSseI32(lua_tonumber(L, index)));
  }
  return openwow::game::TalentInfoStore::Get().GetGroupIndexArg(group_arg);
}

static void PushEmptyTalentTabInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushinteger(L, 0);
  lua_pushnil(L);
  lua_pushinteger(L, 0);
}

static void FirePreviewTalentPointsChangedEvent(bool is_pet,
                                                int talent_index,
                                                int tab_index,
                                                std::uint32_t group_index,
                                                int delta) {
  if (delta == 0) {
    return;
  }

  ScriptEventDispatch::Get().FireEventArgs(
      is_pet ? events::PREVIEW_PET_TALENT_POINTS_CHANGED
             : events::PREVIEW_TALENT_POINTS_CHANGED,
      {talent_index, tab_index, static_cast<int>(group_index + 1), delta});
}

int LuaGetNumTalentTabs(lua_State* L) {
  const bool is_inspect = ReadBoolArgOrDefault(L, 1, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 2, false);
  lua_pushnumber(
      L,
      static_cast<lua_Number>(
          openwow::game::TalentInfoStore::Get().GetTalentTabCount(
              is_inspect, is_pet)));
  return 1;
}

int LuaGetTalentTabInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(
        L,
        "Usage: GetTalentTabInfo(tabIndex[, isInspect[, isPet[, groupIndex]]])");
  }

  const std::uint32_t tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto* dbc = GetDbc(L);
  const bool is_inspect = ReadBoolArgOrDefault(L, 2, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 3, false);
  auto& talent_store = openwow::game::TalentInfoStore::Get();
  const auto* tab_entry =
      talent_store.GetTalentTabArray(tab_index, is_inspect, is_pet);
  const auto* tab_display =
      (dbc && tab_entry) ? dbc->talent_tab().LookupEntry(tab_entry->tab_id) : nullptr;
  if (!tab_display) {
    PushEmptyTalentTabInfo(L);
    return 5;
  }

  lua_pushstring(L, std::string(tab_display->name).c_str());
  if (const auto* icon = dbc->spell_icon().LookupEntry(tab_display->spell_icon_id)) {
    lua_pushstring(L, std::string(icon->icon_path).c_str());
  } else {
    lua_pushnil(L);
  }

  const auto group_index = ResolveTalentGroupIndexArg(L, 4);
  const auto runtime_tab = talent_store.GetTalentGroupTabInfo(
      group_index, tab_index, is_inspect, is_pet);
  const auto points_spent =
      runtime_tab.has_value() ? runtime_tab->points_spent : 0u;
  const auto preview_points_spent = runtime_tab.has_value()
                                        ? runtime_tab->preview_points_spent
                                        : 0;

  lua_pushinteger(L, static_cast<lua_Integer>(points_spent));
  lua_pushstring(L, std::string(tab_display->background_file).c_str());
  lua_pushinteger(L, static_cast<lua_Integer>(preview_points_spent));
  return 5;
}

int LuaGetNumTalents(lua_State* L) {
  if (!lua_isnumber(L, 1))
    return luaL_error(L, "Usage: GetNumTalents(tabIndex[, isInspect[, isPet]])");

  const std::uint32_t tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const bool is_inspect = ReadBoolArgOrDefault(L, 2, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 3, false);
  const auto* tab = openwow::game::TalentInfoStore::Get().GetTalentTabArray(
      tab_index, is_inspect, is_pet);
  lua_pushnumber(
      L, tab ? static_cast<lua_Number>(tab->talent_count) : 0.0);
  return 1;
}

int LuaGetTalentInfo(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2))
    return luaL_error(L, "Usage: GetTalentInfo(tabIndex, talentIndex[, isInspect[, isPet[, groupIndex]]])");

  const auto tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto talent_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2)) - 1u;
  const bool is_inspect = ReadBoolArgOrDefault(L, 3, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 4, false);
  const auto group_index = ResolveTalentGroupIndexArg(L, 5);
  const auto* dbc = GetDbc(L);
  auto* session = GetWorldSession(L);

  auto push_empty = [&]() {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 10;
  };

  auto& store = openwow::game::TalentInfoStore::Get();
  const auto* tab = store.GetTalentTabArray(tab_index, is_inspect, is_pet);
  if (!dbc || tab == nullptr || talent_index >= tab->talents.size()) {
    return push_empty();
  }

  const auto& talent = tab->talents[talent_index];
  if (is_pet && !store.ValidatePetTalent(talent, store.GetPetTalentDataId())) {
    return push_empty();
  }

  const auto* runtime = store.LookupTalentInGroup(group_index, talent.talent_id,
                                                  is_inspect, is_pet);
  const auto current_rank = runtime == nullptr ? -1 : static_cast<int>(runtime->current_rank);
  const auto preview_rank = runtime == nullptr ? -1 : static_cast<int>(runtime->preview_rank);

  const auto selected_rank = current_rank < 0 ? 0u : static_cast<std::uint32_t>(current_rank);
  if (selected_rank >= talent.spell_ids.size() || talent.spell_ids[selected_rank] == 0) {
    return push_empty();
  }
  const auto* spell = dbc->spell().LookupEntry(talent.spell_ids[selected_rank]);
  if (spell == nullptr) {
    return push_empty();
  }

  lua_pushstring(L, std::string(spell->spell_name).c_str());
  if (const auto* icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
      icon != nullptr && !icon->icon_path.empty()) {
    lua_pushstring(L, std::string(icon->icon_path).c_str());
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, talent.tier + 1u);
  lua_pushnumber(L, talent.column + 1u);
  lua_pushnumber(L, current_rank + 1);

  int max_rank = -1;
  for (std::size_t rank = 0; rank < talent.spell_ids.size(); ++rank) {
    if (talent.spell_ids[rank] != 0) {
      max_rank = static_cast<int>(rank);
    }
  }
  lua_pushnumber(L, max_rank + 1);
  lua_pushwowbool(L, (talent.flags & 1u) != 0);

  const auto* player = session == nullptr
                           ? nullptr
                           : session->objects().GetActivePlayer();
  const bool has_owner =
      is_pet ? (player != nullptr && !player->State().GetPetGUID().IsEmpty() &&
                session->objects().GetUnit(player->State().GetPetGUID()) != nullptr)
             : player != nullptr;
  const bool required_spell_satisfied =
      talent.required_spell_id == 0 || is_pet ||
      (player != nullptr &&
       (session->spell_book().HasSpell(talent.required_spell_id) ||
        player->Auras().HasSpellId(talent.required_spell_id)));
  const bool available = is_inspect
                             ? (!is_pet && current_rank >= 0)
                             : (has_owner && required_spell_satisfied);
  lua_pushwowbool(L, available);
  lua_pushnumber(L, preview_rank + 1);
  lua_pushwowbool(L, is_inspect ? (!is_pet && preview_rank >= 0)
                               : (has_owner && required_spell_satisfied));
  return 10;
}

int LuaGetTalentPrereqs(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(
        L, "Usage: GetTalentPrereqs(tabIndex, talentIndex[, isInspect[, isPet[, groupIndex]]])");
  }

  const auto tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto talent_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2)) - 1u;
  const bool is_inspect = ReadBoolArgOrDefault(L, 3, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 4, false);
  const auto group_index = ResolveTalentGroupIndexArg(L, 5);
  auto& store = openwow::game::TalentInfoStore::Get();
  const auto* tab = store.GetTalentTabArray(tab_index, is_inspect, is_pet);
  if (tab == nullptr || talent_index >= tab->talents.size()) {
    return 0;
  }

  const auto& talent = tab->talents[talent_index];
  int result_count = 0;

  for (std::size_t i = 0; i < talent.prereq_talent.size(); ++i) {
    if (talent.prereq_talent[i] == 0) {
      continue;
    }

    const auto prereq_definition = store.FindTalentDefinitionByID(talent.prereq_talent[i]);
    if (!prereq_definition.has_value()) {
      continue;
    }

    const auto* prereq_runtime = store.LookupTalentInGroup(
        group_index, talent.prereq_talent[i], is_inspect, is_pet);
    const bool current_met = prereq_runtime != nullptr &&
                             prereq_runtime->current_rank >=
                                 static_cast<std::int32_t>(talent.prereq_rank[i]);
    const bool preview_met = prereq_runtime != nullptr &&
                             prereq_runtime->preview_rank >=
                                 static_cast<std::int32_t>(talent.prereq_rank[i]);

    lua_pushnumber(L, prereq_definition->tier + 1u);
    lua_pushnumber(L, prereq_definition->column + 1u);
    lua_pushwowbool(L, current_met);
    lua_pushwowbool(L, preview_met);
    result_count += 4;
  }

  return result_count;
}

int LuaLearnTalent(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: LearnTalent(tabIndex, talentIndex[, isPet])");
  }

  const std::uint32_t tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const std::uint32_t talent_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2)) - 1u;
  const bool is_pet = ReadBoolArgOrDefault(L, 3, false);

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  auto& store = openwow::game::TalentInfoStore::Get();

  if (is_pet) {
    if (!store.HasPetTalentData()) return 0;
  } else {
    if (!session->objects().GetActivePlayer()) return 0;
  }

  if (tab_index >= store.GetTalentTabCount(false, is_pet)) return 0;

  const auto* tab = store.GetTalentTabArray(tab_index, false, is_pet);
  if (!tab) return 0;
  if (talent_index >= tab->talents.size()) return 0;

  const auto& talent = tab->talents[talent_index];

  if (is_pet) {
    if (!store.ValidatePetTalent(talent, store.GetPetTalentDataId())) {
      return 0;
    }
  }

  int max_rank = -1;
  for (int r = static_cast<int>(openwow::game::kTalentSpellRankCount) - 1;
       r >= 0; --r) {
    if (talent.spell_ids[r] != 0) {
      max_rank = r;
      break;
    }
  }
  if (max_rank < 0) return 0;

  const std::uint32_t group_index =
      is_pet ? 0u : store.GetDefaultGroupIndex(false);
  int current_rank = -1;
  auto* found = store.FindTalentByID(group_index, talent.talent_id, is_pet);
  if (found) {
    current_rank = static_cast<int>(found->current_rank);
  }

  if (current_rank >= max_rank) return 0;

  const std::uint32_t next_rank = static_cast<std::uint32_t>(current_rank + 1);

  if (is_pet) {

    const auto* player = session->objects().GetActivePlayer();
    if (!player) return 0;
    const std::uint64_t pet_guid = player->State().GetPetGUID().GetRawValue();
    if (pet_guid == 0 ||
        session->objects().GetUnit(openwow::game::ObjectGuid(pet_guid)) == nullptr) {
      return 0;
    }
    session->interaction().SendLearnPetTalent(pet_guid, talent.talent_id,
                                              next_rank);
  } else {

    session->interaction().SendLearnTalent(talent.talent_id, next_rank);
  }

  return 0;
}

int LuaGetActiveTalentGroup(lua_State* L) {
  const bool is_inspect = ReadBoolArgOrDefault(L, 1, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 2, false);
  const auto active_group =
      openwow::game::TalentInfoStore::Get().GetActiveGroupIndexForContext(is_inspect, is_pet);
  lua_pushnumber(L, static_cast<lua_Number>(active_group + 1u));
  return 1;
}

int LuaSetActiveTalentGroup(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetActiveTalentGroup(groupIndex)");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const std::uint32_t group_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto& talent_store = openwow::game::TalentInfoStore::Get();
  const std::uint32_t active_spec = talent_store.GetActiveGroupIndex();
  const std::uint32_t spec_count = talent_store.GetNumGroups();
  if (group_index == active_spec || group_index >= spec_count ||
      group_index >= 2u) {
    return 0;
  }

  session->interaction().SendSetActiveTalentGroup(
      static_cast<std::uint8_t>(group_index));
  return 0;
}

int LuaGetNumTalentGroups(lua_State* L) {
  const bool is_inspect = ReadBoolArgOrDefault(L, 1, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 2, false);
  const auto count =
      openwow::game::TalentInfoStore::Get().GetTalentGroupCount(is_inspect,
                                                                is_pet);
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetUnspentTalentPoints(lua_State* L) {
  const bool is_inspect = ReadBoolArgOrDefault(L, 1, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 2, false);

  auto& talent_info = openwow::game::TalentInfoStore::Get();
  const std::uint32_t group_index =
      (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
          ? (TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 3)) - 1u)
          : talent_info.GetActiveGroupIndex();
  const auto* group =
      talent_info.GetTalentGroupData(group_index, is_inspect, is_pet);
  const auto unspent_points =
      group ? static_cast<lua_Number>(group->unspent_points) : 0.0;
  lua_pushnumber(L, unspent_points);
  return 1;
}

int LuaConfirmTalentWipe(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (!player) return 0;

  const auto& twc = session->spell_book().last_talent_wipe_confirm();
  if (!twc.has_value() || twc->npc_guid == 0) return 0;

  const auto npc_guid = ::openwow::game::ObjectGuid(twc->npc_guid);
  const auto* npc = session->objects().GetUnit(npc_guid);
  if (!npc) return 0;

  const float threshold = npc->State().GetBoundingRadius() + 4.0f;
  const float dx = npc->GetX() - player->GetX();
  const float dy = npc->GetY() - player->GetY();
  const float dz = npc->GetZ() - player->GetZ();
  const float distance_sq = dx * dx + dy * dy + dz * dz;
  if (distance_sq > threshold * threshold) return 0;

  if (twc->cost > player->GetMoney()) {
    DisplaySystemMessage(40);
    return 0;
  }

  session->interaction().SendTalentWipeConfirm(npc_guid);
  return 0;
}

int LuaAddPreviewTalentPoints(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(
        L,
        "Usage: AddPreviewTalentPoints(tabIndex, talentIndex, points[, isPet[, groupIndex]])");
  }

  const std::uint32_t tab_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u;
  const std::uint32_t talent_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 2))) - 1u;
  const int num_points = TruncateLuaNumberToSseI32(lua_tonumber(L, 3));

  const bool is_pet = lua_toboolean(L, 4) != 0;
  const std::uint32_t group_index = ResolveTalentGroupIndexArg(L, 5);
  if (num_points == 0) {
    return 0;
  }

  auto& store = openwow::game::TalentInfoStore::Get();
  const auto* tab = store.GetTalentTabArray(tab_index, false, is_pet);
  if (!tab || talent_index >= tab->talents.size()) {
    return 0;
  }

  const auto before_total = store.GetPreviewPointsSpent(group_index, is_pet);
  store.AddPreviewPoints(group_index, tab_index, talent_index, num_points, is_pet);
  const auto after_total = store.GetPreviewPointsSpent(group_index, is_pet);
  FirePreviewTalentPointsChangedEvent(
      is_pet, static_cast<int>(talent_index + 1),
      static_cast<int>(tab_index + 1), group_index, after_total - before_total);
  return 0;
}

int LuaGetPreviewTalentPointsSpent(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(
        L,
        "Usage: GetPreviewTalentPointsSpent(tabIndex[, isPet[, groupIndex]])");
  }

  (void)TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  (void)ReadBoolArgOrDefault(L, 2, false);
  if (lua_isnumber(L, 3)) {
    (void)TruncateLuaNumberToSseI32(lua_tonumber(L, 3));
  }

  lua_pushinteger(L, 0);
  return 1;
}

int LuaResetPreviewTalentPoints(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(
        L,
        "Usage: ResetPreviewTalentPoints(tabIndex[, isPet[, groupIndex]])");
  }

  const bool is_pet = ReadBoolArgOrDefault(L, 2, false);
  const std::uint32_t group_index = ResolveTalentGroupIndexArg(L, 3);
  const std::uint32_t raw_tab_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));

  auto& store = openwow::game::TalentInfoStore::Get();
  const auto before_total = store.GetPreviewPointsSpent(group_index, is_pet);
  store.ResetPreviewForTab(group_index, raw_tab_index - 1, is_pet);
  const auto after_total = store.GetPreviewPointsSpent(group_index, is_pet);
  FirePreviewTalentPointsChangedEvent(
      is_pet, 0, static_cast<int>(raw_tab_index), group_index,
      after_total - before_total);
  return 0;
}

int LuaResetGroupPreviewTalentPoints(lua_State* L) {
  const bool is_pet = ReadBoolArgOrDefault(L, 1, false);
  const std::uint32_t group_index = ResolveTalentGroupIndexArg(L, 2);

  auto& store = openwow::game::TalentInfoStore::Get();
  const auto before_total = store.GetPreviewPointsSpent(group_index, is_pet);
  store.ResetPreviewForAllTabs(group_index, is_pet);
  const auto after_total = store.GetPreviewPointsSpent(group_index, is_pet);
  FirePreviewTalentPointsChangedEvent(
      is_pet, 0, 0, group_index, after_total - before_total);
  return 0;
}

int LuaGetGroupPreviewTalentPointsSpent(lua_State* L) {
  const bool is_pet = ReadBoolArgOrDefault(L, 1, false);
  const std::uint32_t group_index = ResolveTalentGroupIndexArg(L, 2);
  lua_pushinteger(
      L,
      static_cast<lua_Integer>(
          openwow::game::TalentInfoStore::Get().GetPreviewPointsSpent(
              group_index, is_pet)));
  return 1;
}

static const openwow::data::dbc::DbcLoader* GetDbcExt(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto* dbc = static_cast<const openwow::data::dbc::DbcLoader*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

int LuaGetTalentLink(lua_State* L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(
        L,
        "Usage: GetTalentLink(tabIndex, talentIndex[, isInspect[, isPet[, groupIndex[, isPreview]]]])");
  }

  const std::uint32_t tab_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const std::uint32_t talent_index =
      ::openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2)) - 1u;
  const bool is_inspect = ReadBoolArgOrDefault(L, 3, false);
  const bool is_pet = ReadBoolArgOrDefault(L, 4, false);
  auto& store = openwow::game::TalentInfoStore::Get();
  const auto* tab = store.GetTalentTabArray(tab_index, is_inspect, is_pet);
  if (tab == nullptr || talent_index >= tab->talents.size()) {
    lua_pushnil(L);
    return 1;
  }

  const auto& talent = tab->talents[talent_index];
  if (is_pet &&
      !store.ValidatePetTalent(talent, store.GetPetTalentDataId())) {
    lua_pushnil(L);
    return 1;
  }

  const std::uint32_t group_index = ResolveTalentGroupIndexArg(L, 5);
  auto* learned_talent = store.LookupTalentInGroup(group_index, talent.talent_id,
                                                   is_inspect, is_pet);

  int selected_rank = -1;
  if (learned_talent != nullptr) {

    selected_rank =
        ReadBoolArgOrDefault(L, 5, false)
            ? static_cast<int>(learned_talent->preview_rank)
            : static_cast<int>(learned_talent->current_rank);
  }

  const std::size_t spell_index =
      selected_rank < 0 ? 0u : static_cast<std::size_t>(selected_rank);
  if (spell_index >= talent.spell_ids.size()) {
    lua_pushnil(L);
    return 1;
  }

  const auto spell_id = talent.spell_ids[spell_index];
  if (spell_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  std::string name;
  if (const auto spell = openwow::game::SpellQueryBridge::Get().Query(spell_id);
      spell.has_value() && !spell->name.empty()) {
    name = spell->name;
  } else if (const auto* dbc = GetDbcExt(L); dbc != nullptr) {
    if (const auto* spell_row = dbc->spell().LookupEntry(spell_id);
        spell_row != nullptr && !spell_row->spell_name.empty()) {
      name = std::string(spell_row->spell_name);
    }
  }

  if (name.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(
      L, ::openwow::ui::game::BuildTalentLink(&talent.talent_id, name.c_str(),
                                              selected_rank));
  return 1;
}

int LuaLearnPreviewTalents(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const bool is_pet = ReadBoolArgOrDefault(L, 1, false);
  auto& store = openwow::game::TalentInfoStore::Get();
  const auto group_index =
      store.GetActiveGroupIndexForContext(false, is_pet);
  const auto* group = store.GetTalentGroupData(group_index, false, is_pet);
  if (group == nullptr || store.GetPreviewPointsSpent(group_index, is_pet) <= 0) {
    return 0;
  }

  std::optional<openwow::game::ObjectGuid> pet_guid;
  if (is_pet) {
    const auto* player = session->objects().GetActivePlayer();
    if (player == nullptr || player->State().GetPetGUID().IsEmpty() ||
        session->objects().GetUnit(player->State().GetPetGUID()) == nullptr) {
      return 0;
    }
    pet_guid = player->State().GetPetGUID();
  } else if (session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  std::vector<openwow::game::TalentEntry> entries;
  for (const auto& tab : group->tabs) {
    for (const auto& talent : tab.talents) {
      if (talent.preview_rank <= talent.current_rank ||
          talent.preview_rank < 0 || talent.preview_rank > 8) {
        continue;
      }
      entries.push_back(openwow::game::TalentEntry{
          .talent_id = talent.talent_id,
          .rank = static_cast<std::uint8_t>(talent.preview_rank),
      });
    }
  }

  session->interaction().SendLearnPreviewTalents(entries, pet_guid);
  return 0;
}

}

namespace openwow::ui::game {

namespace {

void FirePreviewTalentsResetEvent(const char* event_name,
                                  const std::int32_t preview_points) {
  ScriptEventDispatch::Get().FireEventArgs(
      event_name,
      {0, 0, 0, -preview_points});
}

}

bool PreviewTalentsCVarValidationCallback(const std::string&,
                                          const std::string&,
                                          const std::string&) {
  auto& talent_info = openwow::game::TalentInfoStore::Get();

  const std::int32_t player_preview_points =
      talent_info.CountTotalPreviewPointsSpent(false);
  talent_info.ResetAllPreviewState(false);
  FirePreviewTalentsResetEvent(events::PREVIEW_TALENT_POINTS_CHANGED,
                               player_preview_points);

  const std::int32_t pet_preview_points =
      talent_info.CountTotalPreviewPointsSpent(true);
  talent_info.ResetAllPreviewState(true);
  FirePreviewTalentsResetEvent(events::PREVIEW_PET_TALENT_POINTS_CHANGED,
                               pet_preview_points);
  return true;
}

}

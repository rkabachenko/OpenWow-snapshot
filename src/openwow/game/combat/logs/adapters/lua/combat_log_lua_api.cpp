#include "openwow/game/combat/logs/adapters/lua/combat_log_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/combat_log.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <cmath>
#include <limits>

namespace openwow::ui::game::detail {

int LuaCombatLog_Object_IsA(lua_State* L) {
  const auto to_uint32 = [](const lua_Number value) {
    if (!std::isfinite(value) || value <= 0.0) {
      return std::uint32_t{0};
    }
    constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
    if (value >= static_cast<lua_Number>(kMax)) {
      return kMax;
    }
    return static_cast<std::uint32_t>(value);
  };

  const std::uint32_t flags = to_uint32(luaL_checknumber(L, 1));
  const std::uint32_t filter = to_uint32(luaL_checknumber(L, 2));
  const std::uint32_t matching = flags & filter;
  const bool is_a = (matching & 0xFFFF0000u) != 0 ||
                    ((matching & 0x000Fu) != 0 &&
                     (matching & 0x00F0u) != 0 &&
                     (matching & 0x0300u) != 0 &&
                     (matching & 0xFC00u) != 0);
  if (is_a) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCombatLogClearEntries(lua_State* L) {

    auto* session = GetWorldSession(L);
    if (!session) return 0;
    session->combat_log().Clear();
    return 0;
}

int LuaCombatLogResetFilter(lua_State* L) {
  if (auto* session = GetWorldSession(L)) {
    session->combat_log().ResetEventFilters();
  }
  return 0;
}

openwow::ui::lua::NativeBindingCatalog CombatLogConstantCatalog() {
  constexpr openwow::ui::LuaNumberGlobal kCombatLogConstants[] = {
      {"COMBATLOG_OBJECT_AFFILIATION_MINE", 0x00000001},
      {"COMBATLOG_OBJECT_AFFILIATION_PARTY", 0x00000002},
      {"COMBATLOG_OBJECT_AFFILIATION_RAID", 0x00000004},
      {"COMBATLOG_OBJECT_AFFILIATION_OUTSIDER", 0x00000008},
      {"COMBATLOG_OBJECT_AFFILIATION_MASK", 0x0000000F},
      {"COMBATLOG_OBJECT_REACTION_FRIENDLY", 0x00000010},
      {"COMBATLOG_OBJECT_REACTION_NEUTRAL", 0x00000020},
      {"COMBATLOG_OBJECT_REACTION_HOSTILE", 0x00000040},
      {"COMBATLOG_OBJECT_REACTION_MASK", 0x000000F0},
      {"COMBATLOG_OBJECT_CONTROL_PLAYER", 0x00000100},
      {"COMBATLOG_OBJECT_CONTROL_NPC", 0x00000200},
      {"COMBATLOG_OBJECT_CONTROL_MASK", 0x00000300},
      {"COMBATLOG_OBJECT_TYPE_PLAYER", 0x0400},
      {"COMBATLOG_OBJECT_TYPE_NPC", 0x0800},
      {"COMBATLOG_OBJECT_TYPE_PET", 0x1000},
      {"COMBATLOG_OBJECT_TYPE_GUARDIAN", 0x2000},
      {"COMBATLOG_OBJECT_TYPE_OBJECT", 0x4000},
      {"COMBATLOG_OBJECT_TYPE_MASK", 0xFC00},
      {"COMBATLOG_OBJECT_TARGET", 0x00010000},
      {"COMBATLOG_OBJECT_FOCUS", 0x00020000},
      {"COMBATLOG_OBJECT_MAINTANK", 0x00040000},
      {"COMBATLOG_OBJECT_MAINASSIST", 0x00080000},
      {"COMBATLOG_OBJECT_RAIDTARGET1", 0x00100000},
      {"COMBATLOG_OBJECT_RAIDTARGET2", 0x00200000},
      {"COMBATLOG_OBJECT_RAIDTARGET3", 0x00400000},
      {"COMBATLOG_OBJECT_RAIDTARGET4", 0x00800000},
      {"COMBATLOG_OBJECT_RAIDTARGET5", 0x01000000},
      {"COMBATLOG_OBJECT_RAIDTARGET6", 0x02000000},
      {"COMBATLOG_OBJECT_RAIDTARGET7", 0x04000000},
      {"COMBATLOG_OBJECT_RAIDTARGET8", 0x08000000},
      {"COMBATLOG_OBJECT_SPECIAL_MASK", 0xFFFF0000LL},
      {"COMBATLOG_OBJECT_NONE", 0x80000000LL},
      {"COMBATLOG_FILTER_ME", 0x00000511},
      {"COMBATLOG_FILTER_MINE", 0x00004511},
      {"COMBATLOG_FILTER_MY_PET", 0x00003111},
      {"COMBATLOG_FILTER_FRIENDLY_UNITS", 0x00007F1E},
      {"COMBATLOG_FILTER_HOSTILE_UNITS", 0x00007E4E},
      {"COMBATLOG_FILTER_HOSTILE_PLAYERS", 0x00007D4E},
      {"COMBATLOG_FILTER_NEUTRAL_UNITS", 0x00007F2E},
      {"COMBATLOG_FILTER_UNKNOWN_UNITS", 0x80000000LL},
      {"COMBATLOG_FILTER_EVERYTHING", 0xFFFFFFFFLL},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.combat.logs", openwow::ui::lua::BindingScope::kWorld,
      kCombatLogConstants);
}

int LuaCombatLogAddFilter(lua_State* L) {
  using namespace openwow::game;

  const auto retail_uint32 = [](const lua_Number value) {
    if (std::isnan(value)) {
      return std::uint32_t{0x80000000u};
    }
    constexpr auto kMax = std::numeric_limits<std::uint32_t>::max();
    if (value >= static_cast<lua_Number>(kMax)) {
      return kMax;
    }
    if (value <= 0.0) {
      return std::uint32_t{0};
    }
    return static_cast<std::uint32_t>(value);
  };
  const auto parse_guid = [](const char* text) {
    if (text == nullptr) {
      return std::uint64_t{0};
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
      text += 2;
    }

    std::uint64_t guid = 0;
    for (std::uint32_t digit_count = 0; digit_count < 16u; ++digit_count) {
      const unsigned char ch = static_cast<unsigned char>(*text++);
      std::uint32_t digit = 0;
      if (ch >= '0' && ch <= '9') {
        digit = ch - '0';
      } else if (ch >= 'a' && ch <= 'f') {
        digit = ch - 'a' + 10u;
      } else if (ch >= 'A' && ch <= 'F') {
        digit = ch - 'A' + 10u;
      } else {
        break;
      }
      guid = (guid << 4u) | digit;
    }
    return guid;
  };
  const auto mask_is_complete = [](const std::uint32_t mask) {
    return (mask & 0xFFFF0000u) != 0u ||
           ((mask & 0x000Fu) != 0u && (mask & 0x00F0u) != 0u &&
            (mask & 0x0300u) != 0u && (mask & 0xFC00u) != 0u);
  };
  const auto read_unit_filter = [&](const int index, const char* error,
                                    std::uint64_t& guid,
                                    std::uint32_t& flags) {
    guid = 0u;
    flags = 0xFFFFFFFFu;
    const int type = lua_type(L, index);
    if (type == LUA_TNUMBER) {
      flags = retail_uint32(lua_tonumber(L, index));
      if (!mask_is_complete(flags)) {
        luaL_error(L, "%s", error);
        return false;
      }
    } else if (type == LUA_TSTRING) {
      guid = parse_guid(lua_tostring(L, index));
      flags = 0u;
    }
    return true;
  };

  const char* event_list = lua_tostring(L, 1);

  std::uint64_t source_guid = 0u;
  std::uint64_t destination_guid = 0u;
  std::uint32_t source_flags = 0u;
  std::uint32_t destination_flags = 0u;
  if (!read_unit_filter(2, "CombatLogAddFilter(): incomplete filter for srcMask",
                        source_guid, source_flags) ||
      !read_unit_filter(3, "CombatLogAddFilter(): incomplete filter for dstMask",
                        destination_guid, destination_flags)) {
    return 0;
  }

  std::uint32_t spell_id = 0u;
  const char* spell_name = nullptr;
  if (lua_type(L, 4) == LUA_TNUMBER) {
    spell_id = retail_uint32(lua_tonumber(L, 4));
  } else if (lua_type(L, 4) == LUA_TSTRING) {
    spell_name = lua_tostring(L, 4);
  }

  CombatLogEventFilter filter;
  CombatLogFilter_SetFilterCriteria(
      filter, event_list, source_guid, source_flags, destination_guid,
      destination_flags, spell_id, spell_name);

  if (lua_type(L, 2) == LUA_TSTRING) {
    filter.src_any = false;
  }
  if (lua_type(L, 3) == LUA_TSTRING) {
    filter.dst_any = false;
  }
  if (auto* session = GetWorldSession(L)) {
    session->combat_log().AddEventFilter(std::move(filter));
  }
  return 0;
}

int LuaCombatLogSetCurrentEntry(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CombatLogSetCurrentEntry(index [,ignoreFilter])");
  }

  auto* session = GetWorldSession(L);
  if (!session) { lua_pushnil(L); return 1; }
  int n             = static_cast<int>(lua_tointeger(L, 1));
  bool ignore_filter = lua_toboolean(L, 2) != 0;
  bool ok = session->combat_log().SetCurrentEntryLua(n, ignore_filter);
  if (ok) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCombatLogSetRetentionTime(lua_State* L) {
  if (lua_type(L, 1) == LUA_TNONE || lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: CombatLogSetRetentionTime(seconds)");
  }

  CVarSystem::SetRegisteredValueOptions options;
  options.force = false;
  options.populate_startup_if_missing = false;
  options.populate_default_if_missing = false;
  options.mark_dirty = true;

  (void)CVarSystem::Instance().SetRegisteredCVarIntValue(
      "combatLogRetentionTime",
      static_cast<int>(lua_tointeger(L, 1)),
      options);
  return 0;
}

int LuaCombatTextSetActiveUnit(lua_State* L) {

  const char* unit_id = luaL_optstring(L, 1, "player");
  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  CombatText_SetActiveUnitGuid(
      ResolveUnitId(session, unit_id != nullptr ? unit_id : "").GetRawValue());
  return 0;
}

int LuaCombatLogAdvanceEntry(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CombatLogAdvanceEntry(count [,ignoreFilter])");
  }

  auto* session = GetWorldSession(L);
  if (!session) { lua_pushnil(L); return 1; }
  int count          = static_cast<int>(lua_tointeger(L, 1));
  bool ignore_filter = lua_toboolean(L, 2) != 0;
  bool ok = session->combat_log().AdvanceEntryLua(count, ignore_filter);
  if (ok) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaCombatLogGetCurrentEntry(lua_State* L) {
    auto* session = GetWorldSession(L);
    if (!session) return 0;
    if (!session->combat_log().current_entry()) return 0;
    return session->combat_log().PushCurrentEntryToLua(L);
}

int LuaCombatLogGetNumEntries(lua_State* L) {
    auto* session = GetWorldSession(L);
    if (!session) { lua_pushinteger(L, 0); return 1; }
    bool ignore_filter = lua_toboolean(L, 1) != 0;
    auto count = session->combat_log().CountFilteredEntries(ignore_filter);
    lua_pushinteger(L, static_cast<lua_Integer>(count));
    return 1;
}

int LuaCombatLogGetRetentionTime(lua_State* L) {
  FrameScript_PushNumberFromInt(
      L, CVarSystem::Instance().GetCVarInt("combatLogRetentionTime"));
  return 1;
}

}

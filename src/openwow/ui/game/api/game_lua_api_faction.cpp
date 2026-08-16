
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_faction.h"
#include "openwow/game/localization.h"
#include "openwow/game/reputation_info.h"
#include "openwow/ui/localized_text_lua.h"
#include "openwow/ui/lua_numeric.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace openwow::ui::game::detail {

static auto& GetReputationInfo(lua_State* L) {
  auto& info = ::openwow::game::ReputationInfo::Get();
  info.BindDbc(GetDbcLoader(L));
  auto* session = GetWorldSession(L);

  if (session != nullptr) {
    if (const auto* player = session->objects().GetActivePlayer(); player != nullptr) {
      info.SetPlayerIdentity(player->State().GetRace(), player->State().GetClass(), player->State().GetGender());
      info.SyncWatchedFactionSlot(player->GetWatchedFactionIndex());
    } else {
      info.ClearPlayerIdentity();
      info.SyncWatchedFactionSlot(std::nullopt);
    }
  } else {
    info.ClearPlayerIdentity();
    info.SyncWatchedFactionSlot(std::nullopt);
  }

  if (info.num_entries() == 0 && GetDbcLoader(L) != nullptr && session != nullptr) {
    info.InitFromPlayerData(session->objects());
  }

  return info;
}

static bool IsActivePlayerInCombat(lua_State* L) {
  if (auto* session = GetWorldSession(L); session != nullptr) {
    if (const auto* player = session->objects().GetActivePlayer();
        player != nullptr) {
      return HasUnitStateFlag(
          static_cast<UnitStateFlag>(player->GetUInt32(UNIT_FIELD_FLAGS)),
          UnitStateFlag::kInCombat);
    }
  }

  return false;
}

static std::size_t ParseSaturatedFactionEntryIndex(lua_State* L,
                                                   const char* usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, usage);
  }

  return static_cast<std::size_t>(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u);
}

static std::size_t ParseTruncatedFactionEntryIndex(lua_State* L,
                                                   const char* usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, usage);
  }

  return static_cast<std::size_t>(
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1))) -
      1u);
}

static void PushOptionalString(lua_State* L, const std::optional<std::string>& value) {
  if (value.has_value()) {
    lua_pushstring(L, value->c_str());
  } else {
    lua_pushnil(L);
  }
}

static void PushOptionalWowBool(lua_State* L, const std::optional<bool>& value) {
  if (value.has_value() && *value) {
    lua_pushwowbool(L, true);
  } else {
    lua_pushnil(L);
  }
}

static void PushFactionInfo(lua_State* L,
                            const ::openwow::game::ReputationInfo::FactionLuaInfo& info) {
  PushOptionalString(L, info.name);
  PushOptionalString(L, info.description);
  lua_pushnumber(L, static_cast<lua_Number>(info.standing_id));
  lua_pushnumber(L, static_cast<lua_Number>(info.bar_min));
  lua_pushnumber(L, static_cast<lua_Number>(info.bar_max));
  lua_pushnumber(L, static_cast<lua_Number>(info.bar_value));
  PushOptionalWowBool(L, info.at_war);
  PushOptionalWowBool(L, info.can_toggle_at_war);
  PushOptionalWowBool(L, info.is_header);
  PushOptionalWowBool(L, info.is_collapsed);
  PushOptionalWowBool(L, info.is_player_friendly);
  PushOptionalWowBool(L, info.is_watched);
  PushOptionalWowBool(L, info.is_child);
}

static void ToggleAllFactionHeaders(lua_State* L, const bool collapse) {
  auto& info = GetReputationInfo(L);
  info.ToggleHeaderCollapse(info.num_entries(), collapse);
}

int LuaGetNumFactions(lua_State* L) {
  auto& info = GetReputationInfo(L);
  lua_pushnumber(L, static_cast<lua_Integer>(info.num_visible()));
  return 1;
}

int LuaGetFactionInfo(lua_State* L) {
  const auto entry_index = ParseSaturatedFactionEntryIndex(
      L, "Usage: GetFactionInfo(index)");
  auto& info = GetReputationInfo(L);
  const auto faction_id = info.GetFactionIdByIndex(entry_index);
  PushFactionInfo(L, info.PushFactionInfo(faction_id));
  return 13;
}

int LuaGetFactionInfoByID(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetFactionInfoByID(id)");
  }
  const auto faction_id =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  auto& info = GetReputationInfo(L);
  PushFactionInfo(L, info.PushFactionInfo(faction_id));
  return 13;
}

int LuaExpandFactionHeader(lua_State* L) {
  GetReputationInfo(L).ToggleHeaderCollapse(
      ParseTruncatedFactionEntryIndex(L, "Usage: ExpandFactionHeader(index)"),
      false);
  return 0;
}

int LuaCollapseFactionHeader(lua_State* L) {
  GetReputationInfo(L).ToggleHeaderCollapse(
      ParseTruncatedFactionEntryIndex(L, "Usage: CollapseFactionHeader(index)"),
      true);
  return 0;
}

int LuaCollapseAllFactionHeaders(lua_State* L) {
  ToggleAllFactionHeaders(L, true);
  return 0;
}

int LuaExpandAllFactionHeaders(lua_State* L) {
  ToggleAllFactionHeaders(L, false);
  return 0;
}

int LuaSetFactionActive(lua_State* L) {
  const auto entry_index =
      ParseSaturatedFactionEntryIndex(L, "Usage: SetFactionActive(index)");
  auto& info = GetReputationInfo(L);
  const auto faction_id = info.GetFactionIdByIndex(entry_index);
  if (faction_id != 0) {
    info.SendSetInactive(faction_id, false);
  }
  return 0;
}

int LuaSetFactionInactive(lua_State* L) {
  const auto entry_index =
      ParseSaturatedFactionEntryIndex(L, "Usage: SetFactionInactive(index)");
  auto& info = GetReputationInfo(L);
  const auto faction_id = info.GetFactionIdByIndex(entry_index);
  if (faction_id != 0) {
    info.SendSetInactive(faction_id, true);
  }
  return 0;
}

int LuaIsFactionInactive(lua_State* L) {
  auto& info = GetReputationInfo(L);
  const auto entry_index = ParseTruncatedFactionEntryIndex(
      L, "Usage: IsFactionInactive(index)");
  lua_pushwowbool(L, info.IsInactive(entry_index));
  return 1;
}

int LuaSetWatchedFactionIndex(lua_State* L) {
  auto& info = GetReputationInfo(L);
  const auto entry_index = ParseTruncatedFactionEntryIndex(
      L, "Usage: SetWatchedFactionIndex(index)");
  const auto faction_id = info.GetFactionIdByIndex(entry_index);
  info.SendSetWatchedFaction(faction_id);
  return 0;
}

int LuaGetWatchedFactionInfo(lua_State* L) {
  auto& info = GetReputationInfo(L);
  const auto faction_id = info.GetWatchedFactionId();
  if (faction_id == 0) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 5;
  }
  const auto lua_info = info.PushFactionInfo(faction_id);
  if (!lua_info.name.has_value()) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 5;
  }
  lua_pushstring(L, lua_info.name->c_str());
  lua_pushnumber(L, static_cast<lua_Number>(lua_info.standing_id));
  lua_pushnumber(L, static_cast<lua_Number>(lua_info.bar_min));
  lua_pushnumber(L, static_cast<lua_Number>(lua_info.bar_max));
  lua_pushnumber(L, static_cast<lua_Number>(lua_info.bar_value));
  return 5;
}

int LuaGetSelectedFaction(lua_State* L) {
  const auto selected_index = GetReputationInfo(L).GetSelectedFactionIndex();
  lua_pushnumber(L, static_cast<lua_Number>(selected_index + 1));
  return 1;
}

int LuaSetSelectedFaction(lua_State* L) {
  auto& info = GetReputationInfo(L);
  const auto entry_index = ParseTruncatedFactionEntryIndex(
      L, "Usage: SetSelectedFaction(index)");
  const auto faction_id = info.GetFactionIdByIndex(entry_index);
  info.SetSelectedFaction(faction_id);
  return 0;
}

int LuaFactionToggleAtWar(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: FactionToggleAtWar(index)");
  }

  auto& info = GetReputationInfo(L);
  const auto faction_id = info.GetFactionIdByIndex(
      static_cast<std::size_t>(
          openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u));
  if (faction_id != 0) {
    info.SendToggleAtWar(
        faction_id,
        !info.IsAtWar(faction_id),
        IsActivePlayerInCombat(L));
  }
  return 0;
}

int LuaGetText(lua_State* L) {
  return openwow::ui::LuaGetLocalizedGlobalText(L);
}

}


#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_instance.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/group_system.h"
#include "openwow/game/instance_handler.h"
#include "openwow/game/localization.h"
#include "openwow/game/quest_text_parser.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/lua_numeric.h"

#include <ctime>
#include <utility>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint32_t kMapAllowsDifficultyChangeFlag = 0x100;

const openwow::data::dbc::DbcLoader* GetDbcLoaderLocal(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto* dbc = static_cast<const openwow::data::dbc::DbcLoader*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

const openwow::data::dbc::MapEntry* LookupMapEntryForDifficultyChange(
    lua_State* L,
    std::int32_t map_id) {
  if (map_id < 0) {
    return nullptr;
  }

  auto* dbc = GetDbcLoaderLocal(L);
  if (!dbc) {
    return nullptr;
  }

  return dbc->map().LookupEntry(static_cast<std::uint32_t>(map_id));
}

std::int32_t ResolveCurrentMapId(const openwow::game::WorldSession& session) {
  if (session.has_current_map()) {
    return static_cast<std::int32_t>(session.current_map_id());
  }

  const auto world_state_map_id = session.world_states().map_id();
  if (world_state_map_id >= 0) {
    return world_state_map_id;
  }

  return static_cast<std::int32_t>(session.objects().GetMapId());
}

const openwow::data::dbc::MapDifficultyEntry* LookupMapDifficultyEntry(
    const openwow::data::dbc::DbcLoader* dbc,
    const std::uint32_t map_id,
    const std::uint32_t difficulty) {
  return openwow::data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
}

std::uint32_t ResolveSavedInstanceMaxPlayers(
    const openwow::data::dbc::MapEntry& map_entry,
    const openwow::data::dbc::MapDifficultyEntry* map_difficulty) {
  if (map_difficulty != nullptr && map_difficulty->max_players != 0) {
    return map_difficulty->max_players;
  }
  return map_entry.max_players;
}

int PushEmptySavedInstanceInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushboolean(L, 0);
  lua_pushboolean(L, 0);
  lua_pushnumber(L, 0);
  lua_pushboolean(L, 0);
  lua_pushnumber(L, 0);
  lua_pushstring(L, "");
  return 10;
}

const openwow::data::dbc::WorldStateUIEntry* FindVisibleWorldStateUiEntry(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::WorldStateManager& world_states,
    std::size_t visible_index) {
  return world_states.FindVisibleWorldStateUiEntry(dbc, visible_index);
}

std::size_t CountVisibleWorldStateUiEntries(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::game::WorldStateManager& world_states) {
  return world_states.CountVisibleWorldStateUiEntries(dbc);
}

std::int32_t LookupWorldStateUiValue(
    const openwow::game::WorldStateManager& world_states,
    std::uint32_t variable_id) {
  if (variable_id == 0) {
    return 0;
  }
  return world_states.GetWorldState(static_cast<std::int32_t>(variable_id));
}

std::string ExpandWorldStateUiText(
    const openwow::data::dbc::WorldStateUIEntry& entry,
    const openwow::game::WorldStateManager& world_states) {
  char buffer[256] = {};
  const std::string format(entry.text);
  openwow::game::QuestTextParser::ExpandWorldStateFormat(
      format.c_str(),
      buffer,
      sizeof(buffer),
      [&world_states](std::uint32_t variable_id) {
        return LookupWorldStateUiValue(world_states, variable_id);
      },
      world_states.world_state_ui_current_time_seconds(std::time(nullptr)));
  return buffer;
}

const openwow::data::dbc::MapEntry* LookupCurrentMapEntry(
    const openwow::game::WorldSession& session,
    const openwow::data::dbc::DbcLoader* dbc) {
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto map_id = ResolveCurrentMapId(session);
  if (map_id < 0) {
    return nullptr;
  }

  return dbc->map().LookupEntry(static_cast<std::uint32_t>(map_id));
}

const char* ResolveInstanceTypeToken(const std::uint32_t map_type) {
  switch (map_type) {
    case 1:
      return "party";
    case 2:
      return "raid";
    case 3:
      return "pvp";
    case 4:
      return "arena";
    default:
      return "none";
  }
}

std::uint32_t GetCurrentInstanceDifficultyIndex(
    const openwow::game::WorldSession& session) {
  return session.instance_difficulty().difficulty_index;
}

std::string ResolveMapDifficultyName(
    const openwow::data::dbc::MapDifficultyEntry* map_difficulty) {
  if (map_difficulty == nullptr || map_difficulty->difficulty_string.empty()) {
    return {};
  }

  const auto difficulty_key = std::string(map_difficulty->difficulty_string);
  return openwow::game::Localization::Get().GetString(
      difficulty_key,
      difficulty_key);
}

bool CanCurrentPlayerChangeDifficulty(
    lua_State* L,
    openwow::game::WorldSession* session) {
  if (session == nullptr) {
    return false;
  }

  const auto* map_entry = LookupCurrentMapEntry(*session, GetDbcLoaderLocal(L));
  if (map_entry == nullptr ||
      map_entry->map_type !=
          static_cast<std::uint32_t>(openwow::data::dbc::MapType::kRaid) ||
      (map_entry->flags & kMapAllowsDifficultyChangeFlag) == 0) {
    return false;
  }

  auto& group_system = ::openwow::game::GroupSystem::Get();
  if (!group_system.IsInRaid()) {
    return false;
  }

  const auto leader_guid = group_system.GetLeaderGuid();
  if (leader_guid == 0) {
    return false;
  }

  return session->objects().GetActivePlayerGuid().GetRawValue() == leader_guid;
}

constexpr int kDifficultyPermissionMessage = 84;

std::optional<std::uint32_t> ParseDifficultyRequestArgument(
    lua_State* L,
    const char* usage,
    const std::uint32_t max_protocol_difficulty) {
  if (lua_isnumber(L, 1) == 0) {
    luaL_error(L, usage);
  }

  const auto raw_protocol_value = lua_tonumber(L, 1) - 1.0;
  if (!std::isfinite(raw_protocol_value)) {
    return std::nullopt;
  }

  const auto truncated_protocol_value =
      static_cast<std::int64_t>(std::trunc(raw_protocol_value));
  if (truncated_protocol_value < 0 ||
      static_cast<std::uint64_t>(truncated_protocol_value) >
          max_protocol_difficulty) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(truncated_protocol_value);
}

std::optional<std::uint32_t> ParseRaidDifficultyRequestArgument(lua_State* L,
                                                                const char* usage) {
  if (lua_isnumber(L, 1) == 0) {
    luaL_error(L, usage);
  }

  const auto raw_protocol_value = lua_tonumber(L, 1) - 1.0;
  std::int64_t truncated_protocol_value = std::numeric_limits<std::int64_t>::min();
  constexpr double kMinX87I64 = -9223372036854775808.0;
  constexpr double kMaxX87I64Exclusive = 9223372036854775808.0;
  if (std::isfinite(raw_protocol_value) && raw_protocol_value >= kMinX87I64 &&
      raw_protocol_value < kMaxX87I64Exclusive) {
    truncated_protocol_value =
        static_cast<std::int64_t>(std::trunc(raw_protocol_value));
  }

  const auto protocol_difficulty =
      static_cast<std::uint32_t>(truncated_protocol_value);
  if (protocol_difficulty > 3u) {
    return std::nullopt;
  }

  return protocol_difficulty;
}

bool HasTrackedPartyDifficultyState(const openwow::game::GroupSystem& group_system) {
  return group_system.GetTrackedPartyMemberCount() != 0;
}

bool HasTrackedRaidDifficultyState(const openwow::game::GroupSystem& group_system) {
  return group_system.GetRealRaidMemberCount() != 0;
}

bool CanLocalPlayerChangeGroupDifficulty(
    const openwow::game::WorldSession& session,
    const openwow::game::GroupSystem& group_system) {
  if (!HasTrackedPartyDifficultyState(group_system) &&
      !HasTrackedRaidDifficultyState(group_system)) {
    return true;
  }

  return session.objects().GetActivePlayerGuid().GetRawValue() ==
         group_system.GetLeaderGuid();
}

bool CanLocalPlayerSetRaidDifficulty(const openwow::game::WorldSession& session,
                                     const openwow::game::GroupSystem& group_system) {
  const bool has_tracked_group =
      group_system.HasPartyMembers() ||
      group_system.GetRealRaidMemberCount() != 0;
  if (!has_tracked_group) {
    return true;
  }

  return session.objects().GetActivePlayerGuid().GetRawValue() ==
         group_system.GetLeaderGuid();
}

openwow::game::DungeonDifficulty GetEffectiveDungeonDifficultyForUi(
    const openwow::game::GroupSystem& group_system) {
  if (HasTrackedPartyDifficultyState(group_system) &&
      !HasTrackedRaidDifficultyState(group_system)) {
    return group_system.GetPartyDungeonDifficulty();
  }

  return group_system.GetDefaultDungeonDifficulty();
}

std::string GetDungeonDifficultyGlobalStringKey(
    const openwow::game::DungeonDifficulty difficulty) {
  return "DUNGEON_DIFFICULTY" +
         std::to_string(static_cast<std::uint32_t>(difficulty) + 1u);
}

void DisplayDungeonDifficultyChangedMessage(
    const openwow::game::DungeonDifficulty difficulty) {
  const auto difficulty_key = GetDungeonDifficultyGlobalStringKey(difficulty);
  const auto difficulty_name =
      openwow::game::Localization::Get().GetString(difficulty_key, difficulty_key);
  openwow::ui::game::DisplaySystemMessage(503, difficulty_name.c_str());
}

std::pair<std::uint32_t, std::uint32_t> CountInstanceLockEncounters(
    const openwow::data::dbc::DbcLoader* dbc,
    const openwow::data::dbc::MapEntry* map_entry,
    const std::uint32_t difficulty,
    const std::uint32_t encounter_mask) {
  if (dbc == nullptr || map_entry == nullptr) {
    return {0, 0};
  }

  std::uint32_t total = 0;
  std::uint32_t completed = 0;
  for (const auto& encounter : dbc->dungeon_encounter().entries()) {
    if (encounter.map_id != map_entry->id || encounter.difficulty != difficulty) {
      continue;
    }

    ++total;
    if ((encounter_mask & (1u << encounter.bit)) != 0) {
      ++completed;
    }
  }

  return {total, completed};
}

int PushInstanceLockEncounter(lua_State* L,
                              const openwow::data::dbc::DbcLoader* dbc,
                              const openwow::data::dbc::MapEntry* map_entry,
                              const std::uint32_t difficulty,
                              const std::int32_t encounter_index,
                              const std::uint32_t encounter_mask) {
  if (dbc == nullptr || map_entry == nullptr || encounter_index < 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  std::uint32_t current = 0;
  for (const auto& encounter : dbc->dungeon_encounter().entries()) {
    if (encounter.map_id != map_entry->id || encounter.difficulty != difficulty) {
      continue;
    }
    if (static_cast<std::int32_t>(current++) != encounter_index) {
      continue;
    }

    lua_pushstring(L, encounter.name.empty() ? "" : std::string(encounter.name).c_str());
    if (const auto* icon = dbc->spell_icon().LookupEntry(encounter.spell_icon_id);
        icon != nullptr && !icon->icon_path.empty()) {
      lua_pushstring(L, std::string(icon->icon_path).c_str());
    } else {
      lua_pushnil(L);
    }
    lua_pushwowbool(L, (encounter_mask & (1u << encounter.bit)) != 0);
    return 3;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  return 3;
}

}

int LuaIsInInstance(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto* map_entry =
      session != nullptr ? LookupCurrentMapEntry(*session, GetDbcLoaderLocal(L)) : nullptr;
  if (map_entry != nullptr && map_entry->map_type != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  lua_pushstring(L, ResolveInstanceTypeToken(map_entry != nullptr ? map_entry->map_type : 0));
  return 2;
}

int LuaGetInstanceInfo(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoaderLocal(L);
  const auto* map_entry =
      session != nullptr ? LookupCurrentMapEntry(*session, dbc) : nullptr;
  if (session == nullptr || map_entry == nullptr) {
    lua_pushnil(L);
    lua_pushstring(L, "none");
    lua_pushnumber(L, 0);
    lua_pushstring(L, "");
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 7;
  }

  const auto difficulty_index = GetCurrentInstanceDifficultyIndex(*session);
  const auto* map_difficulty =
      LookupMapDifficultyEntry(dbc, map_entry->id, difficulty_index);

  lua_pushstring(L, map_entry->name.empty() ? "" : std::string(map_entry->name).c_str());
  lua_pushstring(L, ResolveInstanceTypeToken(map_entry->map_type));
  lua_pushnumber(L, static_cast<lua_Number>(difficulty_index + 1u));
  const auto difficulty_name = ResolveMapDifficultyName(map_difficulty);
  lua_pushstring(L, difficulty_name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(
      map_difficulty != nullptr ? map_difficulty->max_players : 0u));
  lua_pushnumber(L, static_cast<lua_Number>(
      session->instance_difficulty().player_difficulty_index));
  lua_pushwowbool(L, (map_entry->flags & kMapAllowsDifficultyChangeFlag) != 0);
  return 7;
}

int LuaGetInstanceDifficulty(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto difficulty_index =
      session != nullptr ? GetCurrentInstanceDifficultyIndex(*session) : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(difficulty_index + 1u));
  return 1;
}

int LuaGetDungeonDifficulty(lua_State* L) {
  auto& gs = ::openwow::game::GroupSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(
      static_cast<uint8_t>(gs.GetDungeonDifficulty()) + 1));
  lua_pushnumber(L, static_cast<lua_Number>(
      static_cast<uint8_t>(gs.GetDefaultDungeonDifficulty()) + 1));
  return 2;
}

int LuaSetDungeonDifficulty(lua_State* L) {
  const auto protocol_difficulty = ParseDifficultyRequestArgument(
      L,
      "Usage: SetDungeonDifficulty(difficulty)",
      static_cast<std::uint32_t>(openwow::game::DungeonDifficulty::Heroic));
  if (!protocol_difficulty.has_value()) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  auto& group_system = ::openwow::game::GroupSystem::Get();
  if (!CanLocalPlayerChangeGroupDifficulty(*session, group_system)) {
    DisplaySystemMessage(kDifficultyPermissionMessage);
    return 0;
  }

  const auto previous_effective = GetEffectiveDungeonDifficultyForUi(group_system);
  const auto requested_difficulty =
      static_cast<openwow::game::DungeonDifficulty>(*protocol_difficulty);
  group_system.SetDefaultDungeonDifficulty(requested_difficulty);
  group_system.SetPartyDungeonDifficulty(requested_difficulty);
  if (previous_effective != requested_difficulty) {
    DisplayDungeonDifficultyChangedMessage(requested_difficulty);
  }

  session->interaction().SendSetDungeonDifficulty(*protocol_difficulty);
  return 0;
}

int LuaGetRaidDifficulty(lua_State* L) {

  auto& gs = ::openwow::game::GroupSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(
      static_cast<uint8_t>(gs.GetRaidDifficulty()) + 1));
  lua_pushnumber(L, static_cast<lua_Number>(
      static_cast<uint8_t>(gs.GetDefaultRaidDifficulty()) + 1));
  return 2;
}

int LuaSetRaidDifficulty(lua_State* L) {
  const auto protocol_difficulty =
      ParseRaidDifficultyRequestArgument(L, "Usage: SetRaidDifficulty(difficulty)");
  if (!protocol_difficulty.has_value()) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  auto& group_system = ::openwow::game::GroupSystem::Get();
  if (!CanLocalPlayerSetRaidDifficulty(*session, group_system)) {
    DisplaySystemMessage(kDifficultyPermissionMessage);
    return 0;
  }

  session->RequestRaidDifficultyChange(*protocol_difficulty);
  return 0;
}

int LuaCanChangePlayerDifficulty(lua_State* L) {
  auto* session = GetWorldSession(L);
  const bool can_change = CanCurrentPlayerChangeDifficulty(L, session);

  lua_pushboolean(L, can_change);

  const auto now = static_cast<std::uint32_t>(std::time(nullptr));
  const bool cooldown_ready =
      session == nullptr ||
      !session->instance().IsPlayerDifficultyChangeCooldownActive(now);
  lua_pushboolean(L, cooldown_ready);
  return 2;
}

int LuaChangePlayerDifficulty(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetPlayerDifficulty(\"difficulty\")");
  }

  auto* session = GetWorldSession(L);
  if (!CanCurrentPlayerChangeDifficulty(L, session) || session == nullptr) {
    return 0;
  }

  const auto now = static_cast<std::uint32_t>(std::time(nullptr));
  if (session->instance().IsPlayerDifficultyChangeCooldownActive(now)) {
    const auto duration = ::openwow::game::FormatDifficultyChangeCooldownText(
        session->instance().GetPlayerDifficultyChangeCooldownRemainingMs(now));
    DisplaySystemMessage(714, duration.c_str());
    return 0;
  }

  const auto requested_difficulty =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  if (static_cast<std::uint32_t>(requested_difficulty) < 2u) {
    session->interaction().SendChangePlayerDifficulty(
        static_cast<std::uint32_t>(requested_difficulty));
  }
  return 0;
}

int LuaCanMapChangeDifficulty(lua_State* L) {
  std::int32_t map_id = 0;
  if (auto* session = GetWorldSession(L)) {
    map_id = ResolveCurrentMapId(*session);
  }

  if (lua_isnumber(L, 1)) {

    map_id = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));
  }

  const auto* map_entry = LookupMapEntryForDifficultyChange(L, map_id);
  const bool can_change = map_entry != nullptr &&
      (map_entry->flags & kMapAllowsDifficultyChangeFlag) != 0;
  lua_pushwowbool(L, can_change);
  return 1;
}

int LuaResetInstances(lua_State*) {

  (void)::openwow::net::ClientServices__SendPacket(
      ::openwow::net::wotlk::WorldPacket(
          ::openwow::net::wotlk::Opcode::CMSG_RESET_INSTANCES));
  return 0;
}

int LuaGetNumSavedInstances(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto count = session != nullptr ? session->instance().lockouts().size() : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetSavedInstanceInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetSavedInstanceInfo(index)");
  }

  const auto* session = GetWorldSession(L);
  const auto* dbc = GetDbcLoaderLocal(L);
  if (session == nullptr) {
    return PushEmptySavedInstanceInfo(L);
  }

  const auto& lockouts = session->instance().lockouts();
  const auto lockout_index = static_cast<std::size_t>(
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u);
  if (lockout_index >= lockouts.size()) {
    return PushEmptySavedInstanceInfo(L);
  }

  const auto& lockout = lockouts[lockout_index];
  const auto* map_entry =
      dbc != nullptr ? dbc->map().LookupEntry(lockout.map_id) : nullptr;
  if (map_entry == nullptr) {
    return PushEmptySavedInstanceInfo(L);
  }

  const auto* map_difficulty =
      LookupMapDifficultyEntry(dbc, lockout.map_id, lockout.difficulty);
  const auto lockout_id_low = static_cast<std::uint32_t>(lockout.lockout_id & 0xFFFFFFFFull);
  const auto lockout_id_high = static_cast<std::uint32_t>(lockout.lockout_id >> 32u);

  lua_pushstring(L, std::string(map_entry->name).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(lockout_id_low));
  lua_pushnumber(L, static_cast<lua_Number>(lockout.reset_time));
  lua_pushnumber(L, static_cast<lua_Number>(lockout.difficulty + 1u));
  lua_pushboolean(L, lockout.locked != 0);
  lua_pushboolean(L, lockout.extended != 0);
  lua_pushnumber(L, static_cast<lua_Number>(lockout_id_high));
  lua_pushboolean(L,
                  map_entry->map_type ==
                      static_cast<std::uint32_t>(openwow::data::dbc::MapType::kRaid));
  lua_pushnumber(L, static_cast<lua_Number>(
      ResolveSavedInstanceMaxPlayers(*map_entry, map_difficulty)));
  const auto difficulty_name = ResolveMapDifficultyName(map_difficulty);
  lua_pushstring(L, difficulty_name.c_str());
  return 10;
}

int LuaGetNumWorldStateUI(lua_State* L) {
  const auto* dbc = GetDbcLoaderLocal(L);
  const auto* session = GetWorldSession(L);
  if (!dbc || !session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(
      CountVisibleWorldStateUiEntries(*dbc, session->world_states())));
  return 1;
}

int LuaGetWorldStateUIInfo(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetWorldStateUIInfo(index)");
  }

  const auto* dbc = GetDbcLoaderLocal(L);
  const auto* session = GetWorldSession(L);
  const auto zero_based_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (!dbc || !session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto* entry = FindVisibleWorldStateUiEntry(
      *dbc, session->world_states(),
      static_cast<std::size_t>(zero_based_index));
  if (!entry) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto& world_states = session->world_states();
  const auto state = entry->world_state_id == 0
      ? 1
      : LookupWorldStateUiValue(world_states, entry->world_state_id);
  const auto text = ExpandWorldStateUiText(*entry, world_states);

  lua_pushnumber(L, static_cast<lua_Number>(entry->type));
  lua_pushnumber(L, static_cast<lua_Number>(state));
  lua_pushstring(L, text.c_str());
  lua_pushstring(L, std::string(entry->icon).c_str());
  lua_pushstring(L, std::string(entry->dynamic_icon).c_str());
  lua_pushstring(L, std::string(entry->tooltip).c_str());
  lua_pushstring(L, std::string(entry->dynamic_tooltip).c_str());
  lua_pushstring(L, std::string(entry->extended_ui).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(
      LookupWorldStateUiValue(world_states, entry->extended_ui_state_variable0)));
  lua_pushnumber(L, static_cast<lua_Number>(
      LookupWorldStateUiValue(world_states, entry->extended_ui_state_variable1)));
  lua_pushnumber(L, static_cast<lua_Number>(
      LookupWorldStateUiValue(world_states, entry->extended_ui_state_variable2)));
  return 11;
}

int LuaGetInstanceBootTimeRemaining(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto seconds =
      session
          ? session->instance().GetInstanceBootTimeRemainingSeconds(
                openwow::core::GameClock::GetTickCount32())
          : 0;
  lua_pushnumber(L, static_cast<lua_Number>(seconds));
  return 1;
}

int LuaGetInstanceLockTimeRemaining(lua_State* L) {
  const auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  const auto active_lock = session->instance().GetActiveInstanceLock(
      openwow::core::GameClock::GetTickCount32());
  const auto* dbc = GetDbcLoaderLocal(L);
  const auto* map_entry = LookupCurrentMapEntry(*session, dbc);
  const auto difficulty = GetCurrentInstanceDifficultyIndex(*session);
  const auto [total_encounters, completed_encounters] =
      CountInstanceLockEncounters(dbc, map_entry, difficulty,
                                  active_lock.encounter_mask);

  lua_pushnumber(L, static_cast<lua_Number>(active_lock.remaining_seconds));
  lua_pushboolean(L, active_lock.extend_lock ? 1 : 0);
  lua_pushnumber(L, static_cast<lua_Number>(total_encounters));
  lua_pushnumber(L, static_cast<lua_Number>(completed_encounters));
  return 4;
}

int LuaGetInstanceLockTimeRemainingEncounter(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L,
                      "Usage: GetInstanceLockTimeRemainingEncounter(encounterIndex)");
  }

  const auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto active_lock = session->instance().GetActiveInstanceLock(
      openwow::core::GameClock::GetTickCount32());
  if (active_lock.remaining_ms == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto* dbc = GetDbcLoaderLocal(L);
  const auto* map_entry = LookupCurrentMapEntry(*session, dbc);
  const auto difficulty = GetCurrentInstanceDifficultyIndex(*session);
  const auto encounter_index =
      static_cast<std::int32_t>(lua_tonumber(L, 1)) - 1;
  return PushInstanceLockEncounter(L, dbc, map_entry, difficulty,
                                   encounter_index,
                                   active_lock.encounter_mask);
}

}

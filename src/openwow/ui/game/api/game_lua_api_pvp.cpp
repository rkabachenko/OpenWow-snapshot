
#include "openwow/ui/game/api/game_lua_api_pvp.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/battleground_manager.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/duel_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/world_state_manager.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/world/coordinates/world_coordinates.h"

#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>

namespace openwow::ui::game::detail {

namespace {

constexpr std::size_t kBattlefieldScoreResultCount = 12;
constexpr std::size_t kArenaTeamInfoResultCount = 22;
constexpr std::size_t kInspectArenaTeamInfoResultCount = 18;

static const openwow::data::dbc::DbcLoader *GetDbcLoaderLocal(lua_State *L);

constexpr lua_Number kHonorCurrencyCap = 75000.0;
constexpr lua_Number kArenaCurrencyCap = 10000.0;
constexpr std::uint32_t kBattlefieldVehicleFlag = 0x10000000u;
constexpr std::uint32_t kCommentatorSpectatorFlags2 = 0x00080000u;
constexpr std::uint32_t kCommentatorAdminFlags2 = 0x00400000u;
constexpr std::uint32_t kCommentatorBattlemasterNpcFlag = 0x00100000u;
constexpr float kBattlefieldVehicleCoordEpsilon = 0.00000023841858f;
constexpr float kDegreesToRadians = 0.017453292f;
constexpr float kRadiansToDegrees = 57.29578f;
constexpr std::array<const char *, 5> kBattlefieldVehicleTypeTokens = {
    "Drive", "Fly", "Idle", "Airship Horde", "Airship Alliance"};

const openwow::game::CGPlayer_C* GetLocalPlayer(lua_State* L) {
  const auto* session = GetWorldSession(L);
  return session != nullptr ? session->objects().GetLocalPlayerTyped()
                            : nullptr;
}

bool IsCurrentMapBattleground(const openwow::game::WorldSession &session) {
  if (!session.has_current_map()) {
    return false;
  }

  const auto *map_entry = session.LookupMapEntry(session.current_map_id());
  return map_entry != nullptr &&
         map_entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kBg);
}

void SendCommentatorEnablePacket(const std::uint32_t mode) {
  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorEnable(mode));
}

struct SelectedCommentatorInstanceContext {
  const openwow::game::CommentatorMapInfo* map = nullptr;
  const openwow::game::CommentatorInstanceInfo* instance = nullptr;
};

SelectedCommentatorInstanceContext GetSelectedCommentatorInstanceContext(
    const openwow::game::CommentatorState& commentator) {
  const auto* selected_instance = commentator.GetSelectedInstance();
  if (selected_instance == nullptr) {
    return {};
  }

  for (std::size_t map_index = 0; map_index < commentator.GetMapCount(); ++map_index) {
    const auto* map = commentator.GetMapInfo(map_index);
    if (map == nullptr) {
      continue;
    }

    for (const auto& instance : map->instances) {
      if (instance.key == selected_instance->key &&
          instance.guid == selected_instance->guid) {
        return {.map = map, .instance = &instance};
      }
    }
  }

  return {};
}

const openwow::game::CGPlayer_C* GetCommentatorModeActivePlayer(lua_State* L) {
  const auto* const session = GetWorldSession(L);
  return session != nullptr ? session->objects().GetActivePlayer() : nullptr;
}

const openwow::game::ObjectManager* GetCommentatorObjectManager(lua_State* L) {
  const auto* const session = GetWorldSession(L);
  return session != nullptr ? &session->objects() : nullptr;
}

bool HasCommentatorSpectatorMode(lua_State* L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  return active_player != nullptr &&
         (active_player->State().GetUnitFlags2() & kCommentatorSpectatorFlags2) != 0;
}

std::optional<std::uint32_t> GetCommentatorContextMapId(lua_State* L) {
  if (const auto* session = GetWorldSession(L); session != nullptr) {
    if (!session->has_current_map()) {
      return std::nullopt;
    }
    return session->current_map_id();
  }

  return std::nullopt;
}

bool CanUseCommentatorCameraScripts(lua_State* L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr) {
    return false;
  }

  const auto flags2 = active_player->State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u) {
    return false;
  }

  if ((flags2 & kCommentatorAdminFlags2) != 0u) {
    return true;
  }

  const auto map_id = GetCommentatorContextMapId(L);
  if (!map_id.has_value()) {
    return false;
  }

  const auto* session = GetWorldSession(L);
  const auto* dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  if (dbc == nullptr) {
    dbc = GetDbcLoaderLocal(L);
  }
  if (dbc == nullptr) {
    return false;
  }

  const auto* map_entry = dbc->map().LookupEntry(*map_id);
  return map_entry != nullptr &&
         map_entry->map_type ==
             static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena);
}

openwow::world::WorldCamera* GetActiveCommentatorWorldCamera(lua_State* state) {
  auto* manager = runtime::WorldUiRuntimeContext::FromLua(state);
  return manager != nullptr ? &manager->world_camera() : nullptr;
}

std::size_t GetCommentatorLuaIndex(lua_State* L, const int arg_index) {

  return static_cast<std::size_t>(
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, arg_index)) - 1u);
}

std::uint32_t GetCommentatorLuaWrappedU32(lua_State* L, const int arg_index) {
  return TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, arg_index));
}

bool EqualsIgnoreCaseAscii(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }

  while (*left != '\0' && *right != '\0') {
    const auto lhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*left)));
    const auto rhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*right)));
    if (lhs != rhs) {
      return false;
    }

    ++left;
    ++right;
  }

  return *left == '\0' && *right == '\0';
}

bool HasIgnoreCaseAsciiPrefix(const char *value, const char *prefix) {
  if (value == nullptr || prefix == nullptr) {
    return false;
  }

  while (*prefix != '\0') {
    if (*value == '\0') {
      return false;
    }

    const auto lhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*value)));
    const auto rhs = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(*prefix)));
    if (lhs != rhs) {
      return false;
    }

    ++value;
    ++prefix;
  }

  return true;
}

std::uint32_t ReadRequiredArenaRosterNumber(lua_State* L, const int argument,
                                            const char* usage) {
  if (!lua_isnumber(L, argument)) {
    luaL_error(L, "%s", usage);
  }
  return openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, argument));
}

void FireArenaRosterUpdateForLoadedTeams(const openwow::game::WorldSession& session) {
  for (std::uint8_t slot = 0; slot < 3; ++slot) {
    if (!session.battleground().GetArenaRoster(slot).members.empty()) {
      ScriptEventDispatch::Get().FireEvent(events::ARENA_TEAM_ROSTER_UPDATE);
    }
  }
}

std::uint32_t ParseBattlefieldSortStatSuffix(const char *text) {
  if (text == nullptr) {
    return 0;
  }

  std::uint32_t value = 0;
  if (static_cast<unsigned char>(*text) >= static_cast<unsigned char>('0')) {
    while (*text >= '0' && *text <= '9') {
      value = value * 10u + static_cast<std::uint32_t>(*text - '0');
      ++text;
    }
  }

  return value;
}

std::uint8_t ReadSetPvPFlag(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return 0;
  }

  return static_cast<std::uint8_t>(TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)));
}

struct BattlefieldScorePlayerInfo {
  std::string  name;
  std::uint8_t race_id{0};
  std::uint8_t gender_id{0};
  std::uint8_t class_id{0};
};

std::optional<BattlefieldScorePlayerInfo>
ResolveBattlefieldScorePlayerInfo(
    const openwow::game::ObjectGuid &guid,
    const openwow::game::ObjectManager* object_manager) {
  const auto &score_entries = openwow::game::BattlefieldInfo::Get().GetScoreEntries();
  for (const auto &entry : score_entries) {
    if (entry.player_guid != guid) {
      continue;
    }

    if (!entry.player_name.empty() || entry.race_id != 0 || entry.gender_id != 0 ||
        entry.class_id != 0) {
      return BattlefieldScorePlayerInfo{
          .name = entry.player_name,
          .race_id = entry.race_id,
          .gender_id = entry.gender_id,
          .class_id = entry.class_id,
      };
    }
  }

  if (object_manager == nullptr) {
    return std::nullopt;
  }

  if (const auto *unit = object_manager->GetUnit(guid); unit != nullptr) {
    BattlefieldScorePlayerInfo info{
        .name = unit->GetName(),
        .race_id = unit->State().GetRace(),
        .gender_id = unit->State().GetGender(),
        .class_id = unit->State().GetClass(),
    };
    if (info.name.empty()) {
      if (const auto *name_entry = object_manager->GetNameEntry(guid); name_entry != nullptr) {
        info.name = name_entry->name;
      }
    }
    return info;
  }

  if (const auto *name_entry = object_manager->GetNameEntry(guid); name_entry != nullptr) {
    return BattlefieldScorePlayerInfo{
        .name = name_entry->name,
        .race_id = name_entry->race,
        .gender_id = name_entry->gender,
        .class_id = name_entry->class_id,
    };
  }

  return std::nullopt;
}

const char *BattlefieldScoreClassFileToken(const std::uint8_t class_id) {
  switch (class_id) {
    case 1:
      return "WARRIOR";
    case 2:
      return "PALADIN";
    case 3:
      return "HUNTER";
    case 4:
      return "ROGUE";
    case 5:
      return "PRIEST";
    case 6:
      return "DEATHKNIGHT";
    case 7:
      return "SHAMAN";
    case 8:
      return "MAGE";
    case 9:
      return "WARLOCK";
    case 11:
      return "DRUID";
    default:
      return nullptr;
  }
}

int PushEmptyBattlefieldScoreResult(lua_State *L) {
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return static_cast<int>(kBattlefieldScoreResultCount);
}

const openwow::data::dbc::DbcLoader *GetDbcLoaderLocal(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

bool IsArenaMap(const openwow::data::dbc::DbcLoader *dbc, const std::uint32_t map_id) {
  if (!dbc || map_id == 0) {
    return false;
  }

  const auto *entry = dbc->map().LookupEntry(map_id);
  return entry &&
         entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena);
}

const openwow::data::dbc::VehicleEntry *LookupBattlefieldVehicleEntry(
    const openwow::data::dbc::DbcLoader *dbc, const openwow::game::CGUnit_C &unit) {
  if (!dbc) {
    return nullptr;
  }

  if (const auto *vehicle_entry = unit.Vehicle().GetVehicleEntry(); vehicle_entry != nullptr) {
    return vehicle_entry;
  }

  const auto &movement = unit.GetMovementUpdate();
  if (!movement.HasUpdateFlag(openwow::game::kUpdateFlagVehicle) || movement.vehicle_id == 0) {
    return nullptr;
  }

  return dbc->vehicle().LookupEntry(movement.vehicle_id);
}

bool ProjectBattlefieldVehiclePosition(
    const WorldMapSystem& world_map, const std::uint32_t map_id,
    const float world_x, const float world_y, float& map_x, float& map_y) {
  map_x = 0.0f;
  map_y = 0.0f;
  if (map_id == 0) {
    return false;
  }

  const auto has_non_zero_coordinate = [](const float value) {
    return std::fabs(value) >= kBattlefieldVehicleCoordEpsilon;
  };

  const auto projection =
      world_map.LegacyWorldToMapCoords(map_id, world_x, world_y);
  map_x = projection.x;
  map_y = projection.y;
  return projection.success && has_non_zero_coordinate(map_x) &&
         has_non_zero_coordinate(map_y);
}

const char *GetBattlefieldVehicleTypeToken(const openwow::data::dbc::VehicleEntry &vehicle_entry) {
  if (vehicle_entry.ui_locomotion_type >= kBattlefieldVehicleTypeTokens.size()) {
    return nullptr;
  }

  return kBattlefieldVehicleTypeTokens[vehicle_entry.ui_locomotion_type];
}

void PushOneOrNil(lua_State *L, const bool value) {
  if (value) {
    lua_pushnumber(L, 1.0);
    return;
  }

  lua_pushnil(L);
}

int PushBattlefieldBonusTuple(lua_State *L, const bool has_win,
                              const std::uint32_t winner_honor,
                              const std::uint32_t winner_arena,
                              const std::uint32_t loser_honor) {
  lua_pushboolean(L, has_win ? 1 : 0);
  lua_pushnumber(L, static_cast<lua_Number>(winner_honor));
  lua_pushnumber(L, static_cast<lua_Number>(winner_arena));
  lua_pushnumber(L, static_cast<lua_Number>(loser_honor));
  lua_pushnumber(L, 0.0);
  return 5;
}

const char *WorldPvpStatusToken(const openwow::game::WorldPvpQueueState state) {
  switch (state) {
  case openwow::game::WorldPvpQueueState::kNone:
    return "none";
  case openwow::game::WorldPvpQueueState::kQueued:
    return "queued";
  case openwow::game::WorldPvpQueueState::kConfirm:
    return "confirm";
  case openwow::game::WorldPvpQueueState::kActive:
    return "active";
  }
  return "error";
}

void PushPackedColorChannels(lua_State *L, const std::uint32_t color) {
  lua_pushnumber(L, static_cast<lua_Number>((color >> 16) & 0xFFu) / 255.0);
  lua_pushnumber(L, static_cast<lua_Number>((color >> 8) & 0xFFu) / 255.0);
  lua_pushnumber(L, static_cast<lua_Number>(color & 0xFFu) / 255.0);
}

void PushEmptyArenaTeamInfo(lua_State* L) {
  lua_pushnil(L);
  for (std::size_t index = 1; index < kArenaTeamInfoResultCount; ++index) {
    lua_pushnumber(L, 0.0);
  }
}

void PushWorldPvpAreaName(lua_State *L, const std::uint32_t area_id) {
  if (area_id == 0) {
    lua_pushnil(L);
    return;
  }

  const auto *dbc = GetDbcLoaderLocal(L);
  if (!dbc) {
    lua_pushnil(L);
    return;
  }

  const auto *entry = dbc->area_table().LookupEntry(area_id);
  if (!entry || entry->name.empty()) {
    lua_pushnil(L);
    return;
  }

  lua_pushlstring(L, entry->name.data(), entry->name.size());
}

std::uint32_t ReadClampedBattlefieldIndex(lua_State *L, const int argument,
                                          const char *usage) {
  if (!lua_isnumber(L, argument)) {
    luaL_error(L, "%s", usage);
    return 0;
  }

  return openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, argument));
}

std::uint32_t ReadOneBasedBattlefieldIndex(lua_State *L, const int argument,
                                           const char *usage) {
  return ReadClampedBattlefieldIndex(L, argument, usage) - 1u;
}

std::int32_t ReadBattlefieldQueueSlotIndex(lua_State *L, const char *usage) {
  return openwow::ui::SignedI32FromU32Bits(
      ReadOneBasedBattlefieldIndex(L, 1, usage));
}

bool HasActivePlayerObject(const openwow::game::WorldSession &session) {
  return session.objects().GetActivePlayer() != nullptr;
}

bool IsBattlegroundHolidayActive(openwow::game::WorldSession *session,
                                 const std::uint32_t world_state_id) {
  return session != nullptr && world_state_id != 0 &&
         session->world_states().GetWorldState(static_cast<std::int32_t>(world_state_id)) != 0;
}

bool CanEnterBattlegroundForPlayerLevel(
    const openwow::data::dbc::DbcLoader &dbc,
    const openwow::data::dbc::BattlemasterListEntry &entry,
    const std::uint32_t player_level) {
  if (entry.id == 32) {
    return player_level == 80;
  }

  const auto map_id = entry.map_id[0];
  if (map_id < 0) {
    return false;
  }

  bool found_matching_map = false;
  for (const auto &difficulty : dbc.pvp_difficulty().entries()) {
    if (difficulty.map_id != static_cast<std::uint32_t>(map_id)) {
      if (found_matching_map) {
        return false;
      }
      continue;
    }

    if (player_level >= difficulty.min_level && player_level <= difficulty.max_level) {
      return true;
    }

    found_matching_map = true;
  }

  return false;
}

bool IsArenaBattlemasterEntry(const openwow::data::dbc::BattlemasterListEntry &entry) {
  return entry.instance_type ==
         static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena);
}

bool CanJoinActiveBattlefieldAsGroup(
    const openwow::data::dbc::BattlemasterListEntry &entry) {
  return entry.groups_allowed != 0;
}

bool JoinBattlefieldExceedsGroupSizeLimit(
    const openwow::data::dbc::BattlemasterListEntry &entry) {
  const auto &group_system = openwow::game::GroupSystem::Get();
  return entry.max_group_size < group_system.GetRealPartyMemberCount() ||
         entry.max_group_size < group_system.GetRealRaidMemberCount();
}

std::uint8_t ResolveBattlefieldXpLockFlag(const openwow::game::CGPlayer_C &player) {
  constexpr std::uint32_t kPlayerFlagsNoXpGain = 0x02000000u;
  return player.State().GetLevel() < player.GetMaxLevel() &&
                 (player.GetUInt32(PLAYER_FLAGS) & kPlayerFlagsNoXpGain) != 0
             ? 1u
             : 0u;
}

std::uint32_t ResolveBattlefieldJoinInstanceId(
    const openwow::game::BattlefieldListInfo &battlefield_list,
    const std::uint32_t battlefield_index) {
  if (battlefield_list.battlemaster_guid == 0 ||
      battlefield_index >= battlefield_list.instance_ids.size()) {
    return 0;
  }

  return battlefield_list.instance_ids[battlefield_index];
}

const openwow::data::dbc::BattlemasterListEntry *GetBattlegroundInfoEntryForLua(
    lua_State *L, const std::int32_t index, openwow::game::WorldSession **session_out) {
  auto *session = GetWorldSession(L);
  if (session_out != nullptr) {
    *session_out = session;
  }

  if (session == nullptr) {
    return nullptr;
  }

  if (session->objects().GetLocalPlayerTyped() == nullptr) {
    return nullptr;
  }

  const auto *dbc = GetDbcLoaderLocal(L);
  if (dbc == nullptr) {
    return nullptr;
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  battlefield_info.RefreshBattlegroundInfoEntries(
      *dbc, [session](const std::uint32_t world_state_id) {
        return IsBattlegroundHolidayActive(session, world_state_id);
      },
      false);

  const auto entry_id = battlefield_info.GetBattlegroundInfoEntry(index);
  return entry_id == 0 ? nullptr : dbc->battlemaster_list().LookupEntry(entry_id);
}

void PushEmptyBattlefieldStatus(lua_State *L) {
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnil(L);
}

void PushBattlefieldStatusMapName(
    lua_State *L, const openwow::data::dbc::DbcLoader *dbc,
    const openwow::game::BattlefieldInfo &battlefield_info,
    const openwow::game::BGQueueSlot &slot) {
  if (dbc == nullptr) {
    lua_pushnil(L);
    return;
  }

  if (slot.status == 2) {

    if (slot.client_instance == 32 && !slot.bg_confirm_guid.IsEmpty()) {
      if (const auto *random_entry =
              dbc->battlemaster_list().LookupEntry(static_cast<std::uint32_t>(
                  slot.bg_confirm_guid.GetRawValue() >> 16));
          random_entry != nullptr && random_entry->map_id[0] >= 0) {
        if (const auto *map_entry =
                dbc->map().LookupEntry(static_cast<std::uint32_t>(random_entry->map_id[0]));
            map_entry != nullptr && !map_entry->name.empty()) {
          lua_pushlstring(L, map_entry->name.data(), map_entry->name.size());
          return;
        }
      }
    } else if (const auto *map_entry = dbc->map().LookupEntry(slot.confirm_time);
               map_entry != nullptr && !map_entry->name.empty()) {
      lua_pushlstring(L, map_entry->name.data(), map_entry->name.size());
      return;
    }
  } else if (slot.status == 3) {
    if (const auto active_map_id = battlefield_info.GetActiveBGMapId();
        active_map_id != 0) {
      if (const auto *map_entry = dbc->map().LookupEntry(active_map_id);
          map_entry != nullptr && !map_entry->name.empty()) {
        lua_pushlstring(L, map_entry->name.data(), map_entry->name.size());
        return;
      }
    }
  }

  if (const auto *entry = dbc->battlemaster_list().LookupEntry(slot.client_instance);
      entry != nullptr && !entry->name.empty()) {
    lua_pushlstring(L, entry->name.data(), entry->name.size());
    return;
  }

  lua_pushnil(L);
}

}

int LuaGetBattlefieldStatus(lua_State *L) {
  const auto index = ReadBattlefieldQueueSlotIndex(L, "Usage: GetBattlefieldStatus(index)");
  if (index < 0 || index >= static_cast<std::int32_t>(openwow::game::kMaxBGQueueSlots)) {
    PushEmptyBattlefieldStatus(L);
    return 7;
  }

  const auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  const auto &slot = battlefield_info.GetQueueSlot(static_cast<std::size_t>(index));

  switch (slot.status) {
  case 0:
    lua_pushstring(L, "none");
    break;
  case 1:
    lua_pushstring(L, "queued");
    break;
  case 2:
    lua_pushstring(L, "confirm");
    break;
  case 3:
    lua_pushstring(L, "active");
    break;
  default:
    lua_pushstring(L, "error");
    break;
  }

  PushBattlefieldStatusMapName(L, GetDbcLoaderLocal(L), battlefield_info, slot);

  lua_pushnumber(L, static_cast<lua_Number>(slot.map_id));
  lua_pushnumber(L, static_cast<lua_Number>(slot.arena_type));
  lua_pushnumber(L, static_cast<lua_Number>(slot.is_rated));
  lua_pushnumber(L, static_cast<lua_Number>(slot.bg_type_id));
  lua_pushwowbool(L, slot.is_registered);
  return 7;
}

int LuaGetBattlefieldTimeWaited(lua_State *L) {
  const auto index = ReadBattlefieldQueueSlotIndex(L, "Usage: GetBattlefieldTimeWaited(index)");
  if (index < 0 || index >= static_cast<std::int32_t>(openwow::game::kMaxBGQueueSlots)) {
    lua_pushnumber(L, 0);
    return 1;
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlefieldTimeWaited(
                        static_cast<std::size_t>(index))));
  return 1;
}

int LuaGetBattlefieldEstimatedWaitTime(lua_State *L) {
  const auto index =
      ReadBattlefieldQueueSlotIndex(L, "Usage: GetBattlefieldEstimatedWaitTime(index)");
  if (index < 0 || index >= static_cast<std::int32_t>(openwow::game::kMaxBGQueueSlots)) {
    lua_pushnumber(L, 0);
    return 1;
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlefieldEstimatedWaitTime(
                        static_cast<std::size_t>(index))));
  return 1;
}

int LuaGetBattlefieldPortExpiration(lua_State *L) {
  const auto index = ReadBattlefieldQueueSlotIndex(L, "Usage: GetBattlefieldPortExpiration(index)");
  if (index < 0 || index >= static_cast<std::int32_t>(openwow::game::kMaxBGQueueSlots)) {
    lua_pushnumber(L, 0);
    return 1;
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlefieldPortExpiration(
                        static_cast<std::size_t>(index))));
  return 1;
}

int LuaAcceptBattlefieldPort(lua_State *L) {
  const auto index = ReadBattlefieldQueueSlotIndex(
      L, "Usage: AcceptBattlefieldPort(index, accept)");
  const bool accept = lua_toboolean(L, 2) != 0;
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kBattlefieldQueue)) {
    return 0;
  }

  if (index < 0 || index >= static_cast<std::int32_t>(openwow::game::kMaxBGQueueSlots)) {
    lua_pushnil(L);
    return 1;
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  const bool should_validate_accept_restrictions =
      accept && !IsCurrentMapBattleground(*session);
  if (should_validate_accept_restrictions) {
    const auto *player = session->objects().GetActivePlayer();
    if (player != nullptr) {
      if (HasTeleportActionRestriction(*session, *player)) {
        DisplaySystemMessage(450);
        lua_pushnil(L);
        return 1;
      }
      if (player->State().IsDead()) {
        DisplaySystemMessage(135);
        lua_pushnil(L);
        return 1;
      }
      if (player->GetMovementInfo().HasFlag(kMoveFlagFalling)) {
        DisplaySystemMessage(662);
        lua_pushnil(L);
        return 1;
      }
      if (ActivePlayerHasTransportGuid(*session)) {
        DisplaySystemMessage(136);
        lua_pushnil(L);
        return 1;
      }
      if (::openwow::game::HasNegativeMirrorTimerScale(0)) {
        DisplaySystemMessage(663);
        lua_pushnil(L);
        return 1;
      }
    }
  }

  const auto &slot = battlefield_info.GetQueueSlot(static_cast<std::size_t>(index));
  session->interaction().SendBattlefieldPort(slot.bg_instance_guid.GetRawValue(), accept);

  if (!accept) {

    openwow::game::LCD_OnAcceptBattlefieldPort();
    if (slot.bg_type_id == session->battleground().battlefield_list().bg_type_id) {
      ScriptEventDispatch::Get().FireEvent(events::UPDATE_BATTLEFIELD_STATUS);
    }
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaGetBattlefieldInstanceRunTime(lua_State *L) {
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlefieldInstanceRunTime()));
  return 1;
}

int LuaRequestBattlegroundInstanceInfo(lua_State *L) {

  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L,
                      "Usage: RequestBattlegroundInstanceInfo(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr)
    return 0;

  const auto index = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  if (index < 0 ||
      index >= static_cast<std::int32_t>(battlefield_info.GetBattlegroundInfoCount()))
    return 0;

  const auto bg_type = battlefield_info.GetBattlegroundInfoEntry(index);
  if (bg_type == 0)
    return 0;

  constexpr std::uint8_t kFromWhere = 1;
  session->interaction().SendBattlefieldList(
      bg_type, kFromWhere, ResolveBattlefieldXpLockFlag(*player));
  session->battleground().BeginBattlefieldListRequest(bg_type);
  return 0;
}

int LuaJoinBattlefield(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: JoinBattlefield(index)");
  }

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr) {
    return 0;
  }

  const auto battlefield_index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: JoinBattlefield(index)");
  const bool as_group = ScriptReadBoolArgOrDefault(L, 2, false);
  const bool is_rated = ScriptReadBoolArgOrDefault(L, 3, false);
  const auto &battlefield_list = session->battleground().battlefield_list();
  const auto *entry = dbc == nullptr
                          ? nullptr
                          : dbc->battlemaster_list().LookupEntry(
                                battlefield_list.bg_type_id);
  const bool is_arena = entry != nullptr && IsArenaBattlemasterEntry(*entry);

  if (as_group && entry != nullptr && JoinBattlefieldExceedsGroupSizeLimit(*entry)) {
    DisplaySystemMessage(479);
    return 0;
  }

  if (!is_arena && IsCurrentMapBattleground(*session)) {
    DisplaySystemMessage(484);
    return 0;
  }

  if (is_arena) {
    session->interaction().SendBattlemasterJoinArena(
        static_cast<std::uint8_t>(battlefield_index), as_group, is_rated);
    return 0;
  }

  session->interaction().SendBattlemasterJoin(
      battlefield_list.bg_type_id,
      ResolveBattlefieldJoinInstanceId(battlefield_list, battlefield_index),
      as_group);
  return 0;
}

int LuaLeaveBattlefield(lua_State *L) {

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendLeaveBattlefield();
  return 0;
}

int LuaGetBattlefieldWinner(lua_State *L) {
  const auto winner = openwow::game::BattlefieldInfo::Get().GetBattlefieldWinner();
  if (!winner.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*winner));
  return 1;
}

int LuaGetNumBattlefieldScores(lua_State *L) {
  lua_pushnumber(
      L, static_cast<lua_Number>(openwow::game::BattlefieldInfo::Get().GetFilteredScoreCount()));
  return 1;
}

int LuaGetBattlefieldScore(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldScore(index)");
  }

  (void)openwow::ui::ReserveLuaResultCapacity(
      L, kBattlefieldScoreResultCount, "battlefield score values");

  const auto index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldScore(index)");

  const auto *score = openwow::game::BattlefieldInfo::Get().GetDisplayedScoreEntry(
      static_cast<std::size_t>(index));
  if (score == nullptr) {
    return PushEmptyBattlefieldScoreResult(L);
  }

  const auto* const session = GetWorldSession(L);
  const auto player_info = ResolveBattlefieldScorePlayerInfo(
      score->player_guid, session != nullptr ? &session->objects() : nullptr);
  if (!player_info) {
    return PushEmptyBattlefieldScoreResult(L);
  }

  lua_pushstring(L, player_info->name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(score->killing_blows));
  lua_pushnumber(L, static_cast<lua_Number>(score->honorable_kills));
  lua_pushnumber(L, static_cast<lua_Number>(score->deaths));
  lua_pushnumber(L, static_cast<lua_Number>(score->bonus_honor));
  lua_pushnumber(L, static_cast<lua_Number>(score->faction));
  lua_pushnumber(L, 0);

  const auto race_name = LookupRaceDisplayName(L, player_info->race_id, player_info->gender_id);
  if (!race_name.empty()) {
    lua_pushlstring(L, race_name.data(), race_name.size());
  } else {
    lua_pushnil(L);
  }

  const auto *class_token = BattlefieldScoreClassFileToken(player_info->class_id);
  if (class_token != nullptr) {
    lua_pushstring(L, ClassName(player_info->class_id));
    lua_pushstring(L, class_token);
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(score->damage_done));
  lua_pushnumber(L, static_cast<lua_Number>(score->healing_done));
  return 12;
}

int LuaRequestBattlefieldScoreData(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  if (!battlefield_info.CanRequestScoreData(now_tick)) {
    return 0;
  }

  session->interaction().SendPvpLogData();
  battlefield_info.MarkScoreDataRequested(now_tick);
  return 0;
}

int LuaGetBattlefieldStatInfo(lua_State *L) {
  const auto index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldStatInfo(index)");
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  if (const auto *dbc = GetDbcLoaderLocal(L)) {
    battlefield_info.RefreshBattlegroundStatLayout(*dbc);
    if (const auto *entry =
            battlefield_info.GetBattlegroundStatInfo(*dbc, static_cast<std::size_t>(index))) {
      const std::string text(entry->text);
      const std::string icon(entry->icon);
      const std::string tooltip(entry->tooltip);
      lua_pushlstring(L, text.data(), text.size());
      lua_pushlstring(L, icon.data(), icon.size());
      lua_pushlstring(L, tooltip.data(), tooltip.size());
      return 3;
    }
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  return 3;
}

int LuaGetBattlefieldStatData(lua_State *L) {
  constexpr auto *kUsage =
      "Usage: GetBattlefieldStatData(playerIndex, statIndex)";
  const auto player_index = ReadOneBasedBattlefieldIndex(L, 1, kUsage);
  const auto stat_index = ReadOneBasedBattlefieldIndex(L, 2, kUsage);

  const auto value = openwow::game::BattlefieldInfo::Get().GetBattlegroundStatData(
      static_cast<std::size_t>(player_index), static_cast<std::size_t>(stat_index));
  lua_pushnumber(L, static_cast<lua_Number>(value));
  return 1;
}

int LuaGetNumBattlegroundTypes(lua_State *L) {
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  if (const auto *dbc = GetDbcLoaderLocal(L)) {
    battlefield_info.RefreshBattlegroundInfoEntries(
        *dbc,
        [session = GetWorldSession(L)](const std::uint32_t world_state_id) {
          return IsBattlegroundHolidayActive(session, world_state_id);
        },
        false);
  }

  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlegroundInfoCount()));
  return 1;
}

int LuaGetBattlegroundInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlegroundInfo(index)");
  }

  const auto index = openwow::ui::SignedI32FromU32Bits(
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1))) - 1u);
  openwow::game::WorldSession *session = nullptr;
  const auto *entry = GetBattlegroundInfoEntryForLua(L, index, &session);
  if (entry == nullptr) {
    return 0;
  }

  const auto *dbc = GetDbcLoaderLocal(L);
  const auto *player = session->objects().GetLocalPlayerTyped();
  if (dbc == nullptr || player == nullptr) {
    return 0;
  }

  lua_pushlstring(L, entry->name.data(), entry->name.size());
  PushOneOrNil(L, CanEnterBattlegroundForPlayerLevel(*dbc, *entry, player->State().GetLevel()));
  PushOneOrNil(L, IsBattlegroundHolidayActive(session, entry->holiday_world_state));
  PushOneOrNil(L, entry->id == 32);
  lua_pushnumber(L, static_cast<lua_Number>(entry->id));
  return 5;
}

int LuaGetHolidayBGHonorCurrencyBonuses(lua_State *L) {
  if (const auto *session = GetWorldSession(L)) {
    const auto &list = session->battleground().battlefield_list();
    return PushBattlefieldBonusTuple(L, list.holiday_has_win, list.holiday_winner_honor,
                                     list.holiday_winner_arena,
                                     list.holiday_loser_honor);
  }

  return PushBattlefieldBonusTuple(L, false, 0, 0, 0);
}

int LuaGetRandomBGHonorCurrencyBonuses(lua_State *L) {
  if (const auto *session = GetWorldSession(L)) {
    const auto &list = session->battleground().battlefield_list();
    return PushBattlefieldBonusTuple(L, list.random_has_win, list.random_winner_honor,
                                     list.random_winner_arena, list.random_loser_honor);
  }

  return PushBattlefieldBonusTuple(L, false, 0, 0, 0);
}

int LuaSortBGList(lua_State *L) {
  if (const auto *dbc = GetDbcLoaderLocal(L)) {
    auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
    battlefield_info.RefreshBattlegroundInfoEntries(
        *dbc,
        [session = GetWorldSession(L)](const std::uint32_t world_state_id) {
          return IsBattlegroundHolidayActive(session, world_state_id);
        },
        true);
  }

  lua_pushboolean(L, 1);
  return 1;
}

int LuaGetBattlefieldFlagPosition(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldFlagPosition(index)");
  }

  const auto index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldFlagPosition(index)");

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 3;
  }
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  float x = 0.0f;
  float y = 0.0f;
  const auto has_position =
      battlefield_info.GetBattlefieldFlagMapPosition(session->objects(), index, x, y);
  const auto token = battlefield_info.GetBattlefieldFlagToken(session->objects(), index);

  lua_pushnumber(L, has_position ? static_cast<lua_Number>(x) : 0.0);
  lua_pushnumber(L, has_position ? static_cast<lua_Number>(y) : 0.0);
  if (!token.empty()) {
    lua_pushlstring(L, token.data(), token.size());
  } else {
    lua_pushnil(L);
  }
  return 3;
}

int LuaGetWorldPVPQueueStatus(lua_State *L) {
  const auto index = ReadClampedBattlefieldIndex(
      L, 1, "Usage: GetWorldPVPQueueStatus(index)");
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  const auto status =
      openwow::game::BattlefieldInfo::Get().GetWorldPvpQueueStatus(
          *session, static_cast<std::size_t>(index));
  if (!status) {
    return 0;
  }

  lua_pushstring(L, WorldPvpStatusToken(status->state));
  PushWorldPvpAreaName(L, status->area_id);
  lua_pushnumber(L, static_cast<lua_Number>(status->queue_id));
  lua_pushnumber(L, status->time_left_seconds);
  return 4;
}

int LuaIsActiveBattlefieldArena(lua_State *L) {
  const auto *dbc = GetDbcLoaderLocal(L);
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();

  PushOneOrNil(L, IsArenaMap(dbc, battlefield_info.GetActiveBGMapId()));

  const auto active_slot = battlefield_info.GetActiveSlotIndex();
  const bool is_registered =
      active_slot >= 0 && static_cast<std::size_t>(active_slot) < openwow::game::kMaxBGQueueSlots &&
      battlefield_info.GetQueueSlot(static_cast<std::size_t>(active_slot)).is_registered;
  PushOneOrNil(L, is_registered);
  return 2;
}

int LuaGetBattlefieldArenaFaction(lua_State *L) {
  (void)L;
  if (openwow::game::BattlefieldInfo::Get().GetBattlefieldArenaFactionRaw() != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

int LuaGetBattlefieldTeamInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldTeamInfo(index)");
  }

  const auto team_index = ReadClampedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldTeamInfo(index)");

  if (team_index >= openwow::game::kBattlefieldArenaTeamCount) {
    lua_pushstring(L, nullptr);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 4;
  }

  const auto &team_info = openwow::game::BattlefieldInfo::Get().GetArenaBattlefieldTeamInfo(
      static_cast<std::size_t>(team_index));
  lua_pushstring(L, team_info.name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(team_info.raw_values[0]));
  lua_pushnumber(L, static_cast<lua_Number>(team_info.raw_values[1]));
  lua_pushnumber(L, static_cast<lua_Number>(team_info.raw_values[2]));
  return 4;
}

int LuaIsPVPTimerRunning(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayer() : nullptr;
  if (player) {
    uint32_t pflags =
        player->GetUInt32(static_cast<uint32_t>(openwow::game::EPlayerFields::PLAYER_FLAGS));
    if (pflags & 0x20000) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaGetPVPTimer(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayer() : nullptr;
  if (player) {
    uint32_t pflags =
        player->GetUInt32(static_cast<uint32_t>(openwow::game::EPlayerFields::PLAYER_FLAGS));
    if (pflags & 0x40000) {

      lua_pushnumber(L, 1000);
      return 1;
    }
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaTogglePVP(lua_State *L) {

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendTogglePvP();
  return 0;
}

int LuaGetPVPDesired(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayer() : nullptr;
  if (player) {
    uint32_t pflags =
        player->GetUInt32(static_cast<uint32_t>(openwow::game::EPlayerFields::PLAYER_FLAGS));
    lua_pushnumber(L, static_cast<lua_Number>((pflags >> 9) & 1));
  } else {
    lua_pushnumber(L, 0);
  }
  return 1;
}

int LuaSetPVP(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  session->interaction().SendSetPvP(ReadSetPvPFlag(L));
  return 0;
}

int LuaGetPVPSessionStats(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto kills = player->GetUInt32(PLAYER_FIELD_KILLS);
  lua_pushnumber(L, static_cast<lua_Number>(kills & 0xffffu));
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetUInt32(PLAYER_FIELD_TODAY_CONTRIBUTION)));
  return 2;
}

int LuaGetPVPYesterdayStats(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  const auto kills = player->GetUInt32(PLAYER_FIELD_KILLS);
  lua_pushnumber(L, static_cast<lua_Number>((kills >> 16) & 0xffffu));
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetUInt32(PLAYER_FIELD_YESTERDAY_CONTRIBUTION)));
  return 2;
}

int LuaGetPVPLifetimeStats(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (player == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }

  lua_pushnumber(L, static_cast<lua_Number>(player->GetUInt32(
                        PLAYER_FIELD_LIFETIME_HONORABLE_KILLS)));
  const auto rank = player->GetPvpMedalRank();
  lua_pushnumber(L, static_cast<lua_Number>(rank > 4u ? rank : 0u));
  return 2;
}

int LuaGetHonorCurrency(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto *player = session->objects().GetLocalPlayer();
    if (player) {
      lua_pushnumber(L, player->GetUInt32(PLAYER_FIELD_HONOR_CURRENCY));
      lua_pushnumber(L, kHonorCurrencyCap);
      return 2;
    }
  }
  lua_pushnumber(L, 0);
  lua_pushnumber(L, kHonorCurrencyCap);
  return 2;
}

int LuaGetArenaCurrency(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    const auto *player = session->objects().GetLocalPlayer();
    if (player) {
      lua_pushnumber(L, player->GetUInt32(PLAYER_FIELD_ARENA_CURRENCY));
      lua_pushnumber(L, kArenaCurrencyCap);
      return 2;
    }
  }
  lua_pushnumber(L, 0);
  lua_pushnumber(L, kArenaCurrencyCap);
  return 2;
}

int LuaGetMaxArenaCurrency(lua_State *L) {
  lua_pushnumber(L, kArenaCurrencyCap);
  return 1;
}

int LuaGetArenaTeam([[maybe_unused]] lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetArenaTeam(team)");
  }

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kArenaTeamInfoResultCount, "arena team values");

  auto* session = GetWorldSession(L);
  const auto* player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto slot_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (player == nullptr || slot_index >= 3) {
    PushEmptyArenaTeamInfo(L);
    return result_count;
  }

  const auto player_team =
      player->GetArenaTeamInfo(static_cast<std::uint8_t>(slot_index));
  const auto* team_query = session->arena().FindArenaTeamQuery(player_team.team_id);
  if (player_team.team_id == 0 || team_query == nullptr) {
    if (player_team.team_id != 0 &&
        session->arena().QueueArenaTeamUpdateQuery(player_team.team_id)) {
      session->interaction().SendArenaTeamQuery(player_team.team_id);
    }
    PushEmptyArenaTeamInfo(L);
    return result_count;
  }

  const auto* team_summary =
      openwow::game::ArenaSystem::Get().GetTeam(static_cast<std::uint8_t>(slot_index));

  lua_pushstring(L, team_query->team_name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(team_query->team_type));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->rating : 0));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->played_week : 0));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->won_week : 0));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->played_season : 0));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->won_season : 0));
  lua_pushnumber(L, static_cast<lua_Number>(player_team.weekly_games_played));
  lua_pushnumber(L, static_cast<lua_Number>(player_team.weekly_games_won));
  lua_pushnumber(L, static_cast<lua_Number>(team_summary != nullptr ? team_summary->rank : 0));
  lua_pushnumber(L, static_cast<lua_Number>(player_team.personal_rating));
  PushPackedColorChannels(L, team_query->background_color);
  lua_pushnumber(L, static_cast<lua_Number>(team_query->emblem_style));
  PushPackedColorChannels(L, team_query->emblem_color);
  lua_pushnumber(L, static_cast<lua_Number>(team_query->border_style));
  PushPackedColorChannels(L, team_query->border_color);
  return result_count;
}

int LuaGetBattlefieldInstanceExpiration(lua_State *L) {
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlefieldInstanceExpiration()));
  return 1;
}

int LuaGetBattlefieldMapIconScale(lua_State *L) {
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto active_map_id = openwow::game::BattlefieldInfo::Get().GetActiveBGMapId();
  const auto *map_entry = dbc == nullptr ? nullptr : dbc->map().LookupEntry(active_map_id);
  lua_pushnumber(L, map_entry == nullptr ? 1.0
                                        : static_cast<lua_Number>(map_entry->minimap_icon_scale));
  return 1;
}

int LuaGetNumBattlefieldFlagPositions(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Number>(openwow::game::BattlefieldInfo::Get().GetNumFlags()));
  return 1;
}

int LuaGetNumBattlefieldVehicles(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Number>(
                        openwow::game::BattlefieldInfo::Get().GetBattlefieldVehicleCount()));
  return 1;
}

int LuaReportPlayerIsPVPAFK(lua_State *L) {
  if (!lua_isstring(L, 1))
    return luaL_error(L, "Usage: ReportPlayerIsPVPAFK(\"unit\")");

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto *player = session->objects().GetLocalPlayer();
  if (!player)
    return 0;

  const char *unit_str = lua_tostring(L, 1);
  auto target_guid = ResolveUnitId(session, unit_str ? unit_str : "");
  if (target_guid.IsEmpty())
    return 0;

  const auto player_guid = player->GetGuid();
  if (target_guid == player_guid)
    return 0;

  const auto raw = target_guid.GetRawValue();
  auto &group = openwow::game::GroupSystem::Get();
  if (!group.IsActivePlayerOrPartyMemberGuid(raw) &&
      !group.IsRaidMemberGuid(raw))
    return 0;

  session->interaction().SendReportPvpAfk(raw);
  return 0;
}

int LuaSetArenaTeamRosterSelection(lua_State *L) {
  const auto team_idx = ReadRequiredArenaRosterNumber(
      L, 1, "Usage: SetArenaTeamRosterSelection(team, index)");
  const auto index = ReadRequiredArenaRosterNumber(
      L, 2, "Usage: SetArenaTeamRosterSelection(team, index)");

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto slot = team_idx - 1u;
  if (slot >= 3 || index < 1) {
    session->battleground().SetArenaRosterSelection(3, 0);
    return 0;
  }

  session->battleground().SetArenaRosterSelection(
      static_cast<std::uint8_t>(slot), static_cast<std::size_t>(index - 1));
  return 0;
}

int LuaSetArenaTeamRosterShowOffline(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const bool show_offline = ScriptReadBoolArgOrDefault(L, 1, true);

  if (session->battleground().SetArenaRosterShowOffline(show_offline)) {
    FireArenaRosterUpdateForLoadedTeams(*session);
  }
  return 0;
}

int LuaCanJoinBattlefieldAsGroup(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto *entry =
      dbc->battlemaster_list().LookupEntry(session->battleground().battlefield_list().bg_type_id);
  PushOneOrNil(L, entry != nullptr && CanJoinActiveBattlefieldAsGroup(*entry));
  return 1;
}

int LuaGetArenaTeamGdfInfo(lua_State *L) {
  const auto team_idx = ReadRequiredArenaRosterNumber(
      L, 1, "Usage: GetArenaTeamGdfInfo(team, index)");
  const auto index = ReadRequiredArenaRosterNumber(
      L, 2, "Usage: GetArenaTeamGdfInfo(team, index)");

  auto* session = GetWorldSession(L);
  if (session == nullptr || team_idx < 1 || team_idx > 3 || index < 1) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto* member = session->battleground().GetArenaRosterMember(
      static_cast<std::uint8_t>(team_idx - 1), static_cast<std::size_t>(index - 1));
  if (member == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  lua_pushnumber(L, member->mmr_change);
  lua_pushnumber(L, member->mmr_value);
  return 2;
}

int LuaGetArenaTeamRosterSelection(lua_State *L) {
  const auto team_idx = ReadRequiredArenaRosterNumber(
      L, 1, "Usage: GetArenaTeamRosterSelection(team)");

  auto* session = GetWorldSession(L);
  const auto selection =
      session != nullptr && team_idx >= 1 && team_idx <= 3
          ? session->battleground().GetArenaRosterSelection(
                static_cast<std::uint8_t>(team_idx - 1)) + 1
          : 0;
  lua_pushnumber(L, static_cast<lua_Number>(selection));
  return 1;
}

int LuaGetArenaTeamRosterShowOffline(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session != nullptr && session->battleground().GetArenaRosterShowOffline()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetSelectedBattlefield(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto selected_index =
      session == nullptr ? 0u : session->battleground().GetSelectedBattlefieldListIndex();
  lua_pushnumber(L, static_cast<lua_Number>(selected_index));
  return 1;
}

int LuaGetBattlefieldInstanceInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldInfo(index)");
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetLocalPlayerTyped() == nullptr) {
    return 0;
  }

  const auto index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldInfo(index)");
  const auto &instances = session->battleground().battlefield_list().instance_ids;
  if (static_cast<std::size_t>(index) >= instances.size()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(instances[static_cast<std::size_t>(index)]));
  return 1;
}

int LuaGetInspectArenaTeamData(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetInspectArenaTeamData(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const std::uint32_t slot_index =
      TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)) - 1u;
  if (slot_index >= openwow::game::ArenaHandler::kInspectArenaTeamSlots) {
    return 0;
  }

  const auto *team =
      session->arena().GetInspectArenaTeam(static_cast<std::size_t>(slot_index));
  if (!team) {
    return 0;
  }

  const auto *team_query = session->arena().FindArenaTeamQuery(team->team_id);
  if (!team_query) {
    if (session->arena().QueueInspectArenaTeamQuery(team->team_id)) {
      session->interaction().SendArenaTeamQuery(team->team_id);
    }
    return 0;
  }

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kInspectArenaTeamInfoResultCount, "inspected arena team values");
  lua_pushstring(L, team_query->team_name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(team_query->team_type));
  lua_pushnumber(L, static_cast<lua_Number>(team->rating));
  lua_pushnumber(L, static_cast<lua_Number>(team->games_played));
  lua_pushnumber(L, static_cast<lua_Number>(team->games_won));
  lua_pushnumber(L, static_cast<lua_Number>(team->season_games_played));
  lua_pushnumber(L, static_cast<lua_Number>(team->season_games_won));
  PushPackedColorChannels(L, team_query->background_color);
  lua_pushnumber(L, static_cast<lua_Number>(team_query->emblem_style));
  PushPackedColorChannels(L, team_query->emblem_color);
  lua_pushnumber(L, static_cast<lua_Number>(team_query->border_style));
  PushPackedColorChannels(L, team_query->border_color);
  return result_count;
}

int LuaGetNumBattlefieldStats(lua_State *L) {
  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  if (const auto *dbc = GetDbcLoaderLocal(L)) {
    battlefield_info.RefreshBattlegroundStatLayout(*dbc);
  }

  lua_pushnumber(L, static_cast<lua_Number>(battlefield_info.GetBattlegroundStatCount()));
  return 1;
}

int LuaGetNumBattlefields(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto count = session == nullptr ? 0u : static_cast<std::uint32_t>(
                                                   session->battleground()
                                                       .battlefield_list()
                                                       .instance_ids.size());
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetPVPRankProgress(lua_State *L) {
  lua_pushnumber(L, 0);
  return 1;
}

int LuaSetSelectedBattlefield(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedBattlefield(index)");
  }

  auto *session = GetWorldSession(L);
  if (session != nullptr) {
    const auto index = ReadClampedBattlefieldIndex(
        L, 1, "Usage: SetSelectedBattlefield(index)");
    session->battleground().SetSelectedBattlefieldListIndex(index);
  }
  return 0;
}

int LuaIsBattlefieldArena(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto bg_type_id = session == nullptr
                              ? 0u
                              : session->battleground().battlefield_list().bg_type_id;
  const auto *entry = dbc == nullptr
                          ? nullptr
                          : dbc->battlemaster_list().LookupEntry(bg_type_id);
  PushOneOrNil(L, entry != nullptr && IsArenaBattlemasterEntry(*entry));
  return 1;
}

int LuaPlayerIsPVPInactive(lua_State *L) {
  const LuaCallFrame call{L};
  const auto unit_id = call.require_string(1, "Usage: PlayerIsPVPInactive(\"unit\")");

  auto *session = call.world_session();
  if (!session) {
    return call.boolean(false);
  }

  auto guid = ResolveUnitId(session, unit_id);
  if (!guid.IsEmpty()) {
    static constexpr std::uint32_t kDeserterAuraId = 43681;
    const auto &auras = session->aura().GetAuras(guid.GetRawValue());
    for (const auto &a : auras) {
      if (a.spell_id == kDeserterAuraId) {
        return call.boolean(true);
      }
    }
  }

  return call.boolean(false);
}

int LuaAcceptDuel(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto guid = session->duel().GetFlagGuid().GetRawValue();
  session->interaction().SendDuelAccepted(guid);
  return 0;
}

int LuaCancelDuel(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto guid = session->duel().GetFlagGuid().GetRawValue();
  session->interaction().SendDuelCancelled(guid);
  return 0;
}

int LuaCommentatorSetMode(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return 0;
  }

  const auto truncated_mode = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  SendCommentatorEnablePacket(truncated_mode != 0 ? 1u : 0u);
  return 0;
}

int LuaCommentatorToggleMode([[maybe_unused]] lua_State *L) {
  SendCommentatorEnablePacket(2u);
  return 0;
}

int LuaCommentatorGetMode(lua_State *L) {
  std::uint32_t mode = 0;
  if (const auto* active_player = GetCommentatorModeActivePlayer(L); active_player != nullptr) {
    const auto flags2 = active_player->State().GetUnitFlags2();
    if ((flags2 & kCommentatorSpectatorFlags2) != 0) {
      mode = 1;
      if ((flags2 & kCommentatorAdminFlags2) != 0) {
        mode = 2;
      }
    }
  }

  lua_pushnumber(L, static_cast<lua_Number>(mode));
  return 1;
}

int LuaCommentatorGetNumMaps(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Number>(openwow::game::CommentatorState::Get().GetMapCount()));
  return 1;
}

int LuaCommentatorGetMapInfo(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  auto &commentator = openwow::game::CommentatorState::Get();
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorGetMapInfo(mapIndex)");
  }

  const auto map_index = GetCommentatorLuaIndex(L, 1);
  if (map_index >= commentator.GetMapCount()) {
    return luaL_error(L, "no map at that index");
  }

  const auto &map = *commentator.GetMapInfo(map_index);
  lua_pushnumber(L, static_cast<lua_Number>(map.field0));
  lua_pushnumber(L, static_cast<lua_Number>(map.field1));
  lua_pushnumber(L, static_cast<lua_Number>(map.field2));
  lua_pushnumber(L, static_cast<lua_Number>(map.instances.size()));
  return 4;
}

int LuaCommentatorGetInstanceInfo(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  auto &commentator = openwow::game::CommentatorState::Get();
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorGetMapInfo(mapIndex,instanceIndex)");
  }

  const auto map_index = GetCommentatorLuaIndex(L, 1);
  if (map_index >= commentator.GetMapCount()) {
    return luaL_error(L, "no map at that index");
  }

  const auto &map = *commentator.GetMapInfo(map_index);
  const auto instance_index = GetCommentatorLuaIndex(L, 2);
  if (instance_index >= map.instances.size()) {
    return luaL_error(L, "no instance at that index");
  }

  const auto &instance = map.instances[instance_index];
  const auto guid_raw = instance.guid.GetRawValue();
  lua_pushnumber(L, static_cast<lua_Number>(instance.key.map_id));
  lua_pushnumber(L, static_cast<lua_Number>(instance.extra_u32));
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(guid_raw)));
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(guid_raw >> 32)));
  return 4;
}

int LuaCommentatorEnterInstance(lua_State *L) {
  auto &commentator = openwow::game::CommentatorState::Get();
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto *instance = commentator.GetSelectedInstance();
  if (!instance) {
    return luaL_error(L, "Unable to enter instance.");
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorEnterInstance(
          instance->key, instance->guid.GetRawValue()));
  return 0;
}

int LuaCommentatorExitInstance(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorExitInstance());
  return 0;
}

int LuaCommentatorGetNumPlayers(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorGetNumPlayers(faction)");
  }

  const auto faction_index = GetCommentatorLuaIndex(L, 1);
  if (faction_index >= 2u) {
    return luaL_error(L, "Error: faction index too large");
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  if (!HasCommentatorSpectatorMode(L)) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(commentator.GetTeamPlayerCount(faction_index)));
  return 1;
}

int LuaCommentatorGetPlayerInfo(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorGetPlayerInfo(factionIndex,playerIndex)");
  }

  const auto faction_index = GetCommentatorLuaIndex(L, 1);
  const auto player_index = GetCommentatorLuaIndex(L, 2);
  if (faction_index >= 2u) {
    return luaL_error(L, "Error: faction index too large");
  }

  if (!commentator.GetSelectedInstance()) {
    return luaL_error(L, "Unable to find instance.");
  }

  const auto *player = commentator.GetPlayerInfo(faction_index, player_index);
  if (!player) {
    lua_pushstring(L, "Unknown");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 7;
  }

  lua_pushstring(L, player->name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(player->team_index));
  lua_pushnumber(L, static_cast<lua_Number>(player->field_44));
  lua_pushnumber(L, static_cast<lua_Number>(player->field_48));
  lua_pushnumber(L, static_cast<lua_Number>(player->field_52));
  lua_pushnumber(L, static_cast<lua_Number>(player->field_56));
  lua_pushnumber(L, static_cast<lua_Number>(player->field_08));
  return 7;
}

int LuaCommentatorFollowPlayer(lua_State *L) {
  auto& commentator = openwow::game::CommentatorState::Get();
  if (!CanUseCommentatorCameraScripts(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorFollowPlayer(factionIndex,playerIndex)");
  }

  const auto faction_index = GetCommentatorLuaWrappedU32(L, 1) - 1u;
  const auto player_index_one_based = GetCommentatorLuaWrappedU32(L, 2);
  if (faction_index >= 2u) {
    return luaL_error(L, "Error: faction index too large");
  }

  if (player_index_one_based == 0u) {
    commentator.ClearTrackedCameraGuids();
    return 0;
  }

  if (commentator.GetSelectedInstance() == nullptr) {
    return luaL_error(L, "Unable to find instance.");
  }

  const auto player_index = static_cast<std::size_t>(player_index_one_based - 1u);
  const auto* player = commentator.GetPlayerInfo(faction_index, player_index);
  if (player == nullptr) {
    return 0;
  }

  commentator.SetFollowGuid(player->guid.GetRawValue());
  return 0;
}

int LuaCommentatorLookatPlayer(lua_State *L) {
  auto& commentator = openwow::game::CommentatorState::Get();
  if (!CanUseCommentatorCameraScripts(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorLookatPlayer(factionIndex,playerIndex)");
  }

  const auto faction_index = GetCommentatorLuaWrappedU32(L, 1) - 1u;
  const auto player_index_one_based = GetCommentatorLuaWrappedU32(L, 2);
  if (faction_index >= 2u) {
    return luaL_error(L, "Error: faction index too large");
  }

  if (player_index_one_based == 0u) {
    commentator.ClearTrackedCameraGuids();
    return 0;
  }

  if (commentator.GetSelectedInstance() == nullptr) {
    return luaL_error(L, "Unable to find instance.");
  }

  const auto player_index = static_cast<std::size_t>(player_index_one_based - 1u);
  const auto* player = commentator.GetPlayerInfo(faction_index, player_index);
  if (player == nullptr) {
    return 0;
  }

  commentator.SetLookAtGuid(player->guid.GetRawValue());
  return 0;
}

int LuaCommentatorZoomIn([[maybe_unused]] lua_State *L) {
  openwow::game::CommentatorState::Get().ZoomInFieldOfView();
  return 0;
}

int LuaCommentatorZoomOut([[maybe_unused]] lua_State *L) {
  openwow::game::CommentatorState::Get().ZoomOutFieldOfView();
  return 0;
}

int LuaCommentatorSetCamera(lua_State *L) {
  auto &cs = openwow::game::CommentatorState::Get();
  if (!CanUseCommentatorCameraScripts(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4) ||
      !lua_isnumber(L, 5) || !lua_isnumber(L, 6)) {
    return luaL_error(L, "Usage: CommentatorSetCamera(xPos,yPos,zPos,yaw,pitch,fov)");
  }

  const float cameraPos[3] = {
      static_cast<float>(lua_tonumber(L, 1)),
      static_cast<float>(lua_tonumber(L, 2)),
      static_cast<float>(lua_tonumber(L, 3)),
  };
  if (!openwow::world::IsValidMapCoord(cameraPos)) {
    return 0;
  }

  const float yawRadians = static_cast<float>(lua_tonumber(L, 4)) * kDegreesToRadians;
  const float pitchRadians = static_cast<float>(lua_tonumber(L, 5)) * kDegreesToRadians;
  const float fovRadians = static_cast<float>(lua_tonumber(L, 6)) * kDegreesToRadians;

  if (auto* camera = GetActiveCommentatorWorldCamera(L); camera != nullptr) {
    cs.SetCamera(cameraPos[0], cameraPos[1], cameraPos[2], yawRadians, pitchRadians);
    cs.SetFieldOfView(fovRadians);
    camera->SetYaw(yawRadians);
    camera->SetPitch(pitchRadians);
    camera->SetFov(cs.GetFieldOfView());
  }

  return 0;
}

int LuaCommentatorGetCamera(lua_State *L) {
  if (!CanUseCommentatorCameraScripts(L)) {
    return 0;
  }

  const auto* camera = GetActiveCommentatorWorldCamera(L);
  if (camera == nullptr) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  const auto camera_position = commentator.GetCameraPosition();
  lua_pushnumber(L, static_cast<lua_Number>(camera_position.x));
  lua_pushnumber(L, static_cast<lua_Number>(camera_position.y));
  lua_pushnumber(L, static_cast<lua_Number>(camera_position.z));
  lua_pushnumber(L, static_cast<lua_Number>(camera->yaw() * kRadiansToDegrees));
  lua_pushnumber(L, static_cast<lua_Number>(camera->pitch() * kRadiansToDegrees));
  lua_pushnumber(L,
                 static_cast<lua_Number>(commentator.GetFieldOfView() * kRadiansToDegrees));
  return 6;
}

int LuaCommentatorGetCurrentMapID(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  const auto map_id = commentator.GetCurrentMapId();
  if (!map_id) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*map_id));
  return 1;
}

int LuaCommentatorStartInstance(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr ||
      (active_player->State().GetUnitFlags2() & kCommentatorAdminFlags2) == 0u) {
    return 0;
  }

  auto& commentator = openwow::game::CommentatorState::Get();
  const auto battlemaster_guid = commentator.GetBattlemasterGuid();
  if (battlemaster_guid == 0) {
    return luaL_error(L, "Battlemaster must be set first.");
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) ||
      !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
    return luaL_error(
        L,
        "Usage: CommentatorStartInstance(mapID,teamSize,minLevel,maxLevel)");
  }

  const auto map_id = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
  const auto team_size = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 2));
  const auto min_level = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 3));
  const auto max_level = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 4));
  if (team_size != 2u && team_size != 3u && team_size != 5u) {
    return luaL_error(L, "Unknown team size. Use 2, 3, or 5.");
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorStartInstance(
          battlemaster_guid,
          map_id,
          team_size,
          min_level,
          max_level));
  return 0;
}

int LuaCommentatorAddPlayer(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr ||
      (active_player->State().GetUnitFlags2() & kCommentatorAdminFlags2) == 0u) {
    return 0;
  }

  const auto* const objects = GetCommentatorObjectManager(L);
  if (objects == nullptr) {
    return 0;
  }
  if (objects->GetTargetGuid().IsEmpty()) {
    return luaL_error(L, "A player must be targeted.");
  }

  const auto* target_player = objects->GetPlayer(objects->GetTargetGuid());
  if (target_player == nullptr) {
    return luaL_error(L, "Must target a player.");
  }

  auto& commentator = openwow::game::CommentatorState::Get();
  const auto selection = GetSelectedCommentatorInstanceContext(commentator);
  if (selection.map == nullptr || selection.instance == nullptr) {
    return luaL_error(L, "Must select an instance first.");
  }

  const auto battlemaster_guid = commentator.GetBattlemasterGuid();
  if (battlemaster_guid == 0) {
    return luaL_error(L, "Must set Battlemaster first.");
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorAddPlayer(teamNumber)");
  }

  auto team_index = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1)) - 1u;
  if (team_index > 1u) {
    team_index = 0;
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorAddPlayer(
          *selection.map,
          *selection.instance,
          target_player->GetGuid().GetRawValue(),
          battlemaster_guid,
          team_index));
  return 0;
}

int LuaCommentatorRemovePlayer(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr ||
      (active_player->State().GetUnitFlags2() & kCommentatorAdminFlags2) == 0u) {
    return 0;
  }

  auto& commentator = openwow::game::CommentatorState::Get();
  const auto selection = GetSelectedCommentatorInstanceContext(commentator);
  if (selection.map == nullptr || selection.instance == nullptr) {
    return luaL_error(L, "Must select an instance first.");
  }

  const auto battlemaster_guid = commentator.GetBattlemasterGuid();
  if (battlemaster_guid == 0) {
    return luaL_error(L, "Must set Battlemaster first.");
  }

  const auto team_index = commentator.GetSelectedPlayerTeamIndex();
  if (team_index >= 2u) {
    return luaL_error(L, "Error: Faction of current player is invalid.");
  }

  const auto player_index = commentator.GetSelectedPlayerIndex();
  const auto* selected_player = commentator.GetPlayerInfo(team_index, player_index);
  if (selected_player == nullptr) {
    return luaL_error(L, "Error: Index of current player is invalid.");
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorRemovePlayer(
          *selection.map,
          *selection.instance,
          selected_player->guid.GetRawValue(),
          battlemaster_guid,
          static_cast<std::uint32_t>(team_index)));
  return 0;
}

int LuaCommentatorSetBattlemaster(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto* const objects = GetCommentatorObjectManager(L);
  if (objects == nullptr) {
    return 0;
  }
  const auto target_guid = objects->GetTargetGuid();
  if (target_guid.IsEmpty()) {
    return luaL_error(L, "Battlemaster must be targeted.");
  }

  const auto* target = objects->GetTarget();
  if (target == nullptr ||
      (target->State().GetNpcFlags() & kCommentatorBattlemasterNpcFlag) == 0u) {
    return luaL_error(L, "Targeted unit is not a Battlemaster.");
  }

  openwow::game::CommentatorState::Get().SetBattlemasterGuid(
      target->GetGuid().GetRawValue());
  return 0;
}

int LuaCommentatorSetMoveSpeed(lua_State *L) {
  auto &cs = openwow::game::CommentatorState::Get();
  if (!CanUseCommentatorCameraScripts(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorSetMoveSpeed(speed)");
  }

  float speed = static_cast<float>(lua_tonumber(L, 1));
  cs.SetMoveSpeed(speed);
  return 0;
}

int LuaCommentatorSetCameraCollision(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr) {
    return 0;
  }

  const auto flags2 = active_player->State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u ||
      (flags2 & kCommentatorAdminFlags2) == 0u) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorSetCameraCollision(bool enable)");
  }

  openwow::game::CommentatorState::Get().SetCameraCollisionEnabled(lua_tonumber(L, 1) != 0.0);
  return 0;
}

int LuaCommentatorSetTargetHeightOffset(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr) {
    return 0;
  }

  const auto flags2 = active_player->State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u ||
      (flags2 & kCommentatorAdminFlags2) == 0u) {
    return 0;
  }

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: CommentatorSetTargetHeightOffset(float offset)");
  }

  openwow::game::CommentatorState::Get().SetTargetHeightOffset(
      static_cast<float>(lua_tonumber(L, 1)));
  return 0;
}

int LuaCommentatorSetMapAndInstanceIndex(lua_State *L) {
  auto &commentator = openwow::game::CommentatorState::Get();
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorSetMapAndInstanceIndex(mapIndex,instanceIndex)");
  }

  const auto map_index = GetCommentatorLuaIndex(L, 1);
  if (map_index >= commentator.GetMapCount()) {
    return luaL_error(L, "no map at that index");
  }

  const auto *map = commentator.GetMapInfo(map_index);
  const auto instance_index = GetCommentatorLuaIndex(L, 2);
  if (!map || instance_index >= map->instances.size()) {
    return luaL_error(L, "no instance at that index");
  }

  (void)commentator.SelectMapAndInstance(map_index, instance_index);
  return 0;
}

int LuaCommentatorSetPlayerIndex(lua_State *L) {
  auto &commentator = openwow::game::CommentatorState::Get();
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: CommentatorSetPlayerIndex(factionIndex,playerIndex)");
  }

  const auto faction_index = GetCommentatorLuaIndex(L, 1);
  const auto player_index = GetCommentatorLuaIndex(L, 2);
  if (faction_index >= 2u) {
    return luaL_error(L, "Error: faction index too large");
  }

  if (!commentator.GetSelectedInstance()) {
    return luaL_error(L, "Unable to find instance.");
  }

  if (!commentator.SetSelectedPlayer(faction_index, player_index)) {
    return luaL_error(L, "Error: player index too large");
  }

  return 0;
}

int LuaCommentatorUpdatePlayerInfo(lua_State *L) {
  auto &commentator = openwow::game::CommentatorState::Get();
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto *instance = commentator.GetSelectedInstance();
  if (!instance) {
    return 0;
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorGetPlayerInfo(instance->key));
  return 0;
}

int LuaCommentatorUpdateMapInfo(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const char* zone = "";
  if (lua_isstring(L, 1) != 0) {
    if (const char* value = lua_tostring(L, 1); value != nullptr) {
      zone = value;
    }
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorGetMapInfo(zone));
  return 0;
}

int LuaCommentatorSetSkirmishMatchmakingMode(lua_State *L) {
  const auto* active_player = GetCommentatorModeActivePlayer(L);
  if (active_player == nullptr) {
    return 0;
  }

  const auto flags2 = active_player->State().GetUnitFlags2();
  if ((flags2 & kCommentatorSpectatorFlags2) == 0u ||
      (flags2 & kCommentatorAdminFlags2) == 0u || lua_isnumber(L, 1) == 0) {
    return 0;
  }

  const auto mode = static_cast<std::uint8_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorSetSkirmishMatchmakingMode(mode));
  return 0;
}

int LuaCommentatorRequestSkirmishQueueData(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorRequestSkirmishQueueData());
  return 0;
}

int LuaCommentatorGetSkirmishQueueCount(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  lua_pushnumber(L, static_cast<lua_Number>(commentator.GetSkirmishQueueCount()));
  return 1;
}

int LuaCommentatorGetSkirmishQueuePlayerInfo(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L) || !lua_isnumber(L, 1)) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  const auto *entry = commentator.GetSkirmishQueueEntry(GetCommentatorLuaIndex(L, 1));
  if (!entry) {
    return 0;
  }

  const auto first_guid = entry->first_guid.ToHexString();
  const auto second_guid = entry->second_guid.ToHexString();
  lua_pushstring(L, first_guid.c_str());
  lua_pushstring(L, second_guid.c_str());
  lua_pushboolean(L, entry->rated ? 1 : 0);
  return 3;
}

int LuaCommentatorStartSkirmishMatch(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L) || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
    return 0;
  }

  const auto first_guid = openwow::game::ObjectGuid::FromHexString(lua_tostring(L, 1));
  const auto second_guid = openwow::game::ObjectGuid::FromHexString(lua_tostring(L, 2));
  auto match_size = static_cast<std::int32_t>(-1);
  if (lua_isnumber(L, 3)) {
    match_size = TruncateLuaNumberToSseI32(lua_tonumber(L, 3));
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorStartSkirmishMatch(
          first_guid.GetRawValue(), second_guid.GetRawValue(), match_size));
  return 0;
}

int LuaCommentatorRequestSkirmishMode(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  (void)openwow::net::ClientServices__SendPacket(
      openwow::net::wotlk::PacketSender::BuildCommentatorRequestSkirmishMode());
  return 0;
}

int LuaCommentatorGetSkirmishMode(lua_State *L) {
  if (!HasCommentatorSpectatorMode(L)) {
    return 0;
  }

  const auto &commentator = openwow::game::CommentatorState::Get();
  lua_pushnumber(L, static_cast<lua_Number>(commentator.GetSkirmishMode()));
  return 1;
}

int LuaBattlefieldMgrEntryInviteResponse(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !HasActivePlayerObject(*session)) {
    return 0;
  }

  const auto battlefield_id =
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  const bool accepted = ScriptReadBoolArgOrDefault(L, 2, false);
  session->interaction().SendBattlefieldMgrEntryInviteResponse(battlefield_id,
                                                               accepted);
  return 0;
}

int LuaBattlefieldMgrQueueRequest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !HasActivePlayerObject(*session)) {
    return 0;
  }

  const auto battlefield_id =
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  session->interaction().SendBattlefieldMgrQueueRequest(battlefield_id);
  return 0;
}

int LuaBattlefieldMgrQueueInviteResponse(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !HasActivePlayerObject(*session)) {
    return 0;
  }

  const auto battlefield_id =
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  const bool accepted = ScriptReadBoolArgOrDefault(L, 2, false);
  session->interaction().SendBattlefieldMgrQueueInviteResponse(battlefield_id,
                                                               accepted);
  return 0;
}

int LuaBattlefieldMgrExitRequest(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !HasActivePlayerObject(*session)) {
    return 0;
  }

  const auto battlefield_id =
      static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  session->interaction().SendBattlefieldMgrExitRequest(battlefield_id);
  return 0;
}

int LuaGetBattlefieldInfo(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  const auto *dbc = GetDbcLoaderLocal(L);
  if (player == nullptr || dbc == nullptr) {
    return 0;
  }

  const auto bg_type_id = session->battleground().battlefield_list().bg_type_id;
  const auto *entry = dbc->battlemaster_list().LookupEntry(bg_type_id);
  if (entry == nullptr) {
    return 0;
  }

  const auto map_id = entry->map_id[0];
  const auto *map_entry =
      map_id < 0 ? nullptr : dbc->map().LookupEntry(static_cast<std::uint32_t>(map_id));
  if (map_entry == nullptr) {
    return 0;
  }

  const std::string battleground_name(entry->name);
  const std::string map_name(map_entry->name);

  lua_pushstring(L, battleground_name.c_str());
  lua_pushstring(L, map_name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(entry->max_group_size));
  PushOneOrNil(L, CanEnterBattlegroundForPlayerLevel(*dbc, *entry, player->State().GetLevel()));
  PushOneOrNil(L, IsBattlegroundHolidayActive(session, entry->holiday_world_state));
  PushOneOrNil(L, entry->id == 32);
  return 6;
}

int LuaGetBattlefieldVehicleInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldVehicleInfo(index)");
  }

  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto index = ReadOneBasedBattlefieldIndex(
      L, 1, "Usage: GetBattlefieldVehicleInfo(index)");
  const auto guid = openwow::game::BattlefieldInfo::Get().GetBattlefieldVehicleGuid(
      index);
  if (guid.IsEmpty()) {
    return 0;
  }

  if (session == nullptr) {
    return 0;
  }
  const auto *unit = session->objects().GetUnit(guid);
  const auto* const game_ui = runtime::WorldUiRuntimeContext::FromLua(L);
  if (unit == nullptr || game_ui == nullptr) {
    return 0;
  }

  float map_x = 0.0f;
  float map_y = 0.0f;
  const auto current_map_id = session->current_map_id();
  if (!ProjectBattlefieldVehiclePosition(
          game_ui->world_map(), current_map_id, unit->GetX(), unit->GetY(),
          map_x, map_y)) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(map_x));
  lua_pushnumber(L, static_cast<lua_Number>(map_y));

  lua_pushstring(L, unit->ResolveRetailName(*session).c_str());
  lua_pushboolean(L, unit->State().IsPossessed() ? 1 : 0);

  if (const auto *vehicle_entry = LookupBattlefieldVehicleEntry(dbc, *unit);
      vehicle_entry != nullptr && (vehicle_entry->flags & kBattlefieldVehicleFlag) != 0) {
    if (const auto *type_token = GetBattlefieldVehicleTypeToken(*vehicle_entry)) {
      lua_pushstring(L, type_token);
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(unit->GetOrientation()));
  lua_pushboolean(L, unit->Movement().CanControlCharacter() ? 1 : 0);
  lua_pushboolean(L, unit->State().IsDead() ? 0 : 1);
  return 8;
}

int LuaSortBattlefieldScoreData(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usgae: SortBattlefieldScoreData(\"type\")");
  }

  const char *sort_type = lua_tostring(L, 1);
  std::uint32_t column = 0;
  if (EqualsIgnoreCaseAscii(sort_type, "kills")) {
    column = 0;
  } else if (EqualsIgnoreCaseAscii(sort_type, "deaths")) {
    column = 1;
  } else if (EqualsIgnoreCaseAscii(sort_type, "cp")) {
    column = 2;
  } else if (EqualsIgnoreCaseAscii(sort_type, "name")) {
    column = 3;
  } else if (EqualsIgnoreCaseAscii(sort_type, "class")) {
    column = 4;
  } else if (EqualsIgnoreCaseAscii(sort_type, "hk")) {
    column = 5;
  } else if (EqualsIgnoreCaseAscii(sort_type, "damage")) {
    column = 6;
  } else if (EqualsIgnoreCaseAscii(sort_type, "healing")) {
    column = 7;
  } else if (EqualsIgnoreCaseAscii(sort_type, "team")) {
    column = 8;
  } else if (HasIgnoreCaseAsciiPrefix(sort_type, "stat")) {
    column = ParseBattlefieldSortStatSuffix(sort_type + 4) + 8u;
    if (column >= 17u) {
      return 0;
    }
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  openwow::game::BattlefieldInfo::Get().PromoteScoreSortColumn(
      session->objects(), column);
  return 0;
}

}

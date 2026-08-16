
#include "openwow/ui/game/api/game_lua_api_lfg.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/runtime/scheduling/burst_throttle.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/group_system.h"
#include "openwow/game/lfg_system.h"
#include "openwow/game/localization.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/packed_choice_state.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/loot_tooltip_support.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <functional>
#include <limits>
#include <string_view>
#include <utility>

namespace openwow::ui::game::detail {

namespace {

static constexpr const char *kGameUiMgrKey = "openwow.world_ui_runtime_context";
constexpr std::size_t kLfgCommentMaxBytes = 0xFF;
constexpr std::uint32_t kPackedDungeonIdMask = 0x00FFFFFFu;
constexpr int kLfgCommentLeadByteCutoff = 0x40;
constexpr std::size_t kSearchExtendedResultCount = 28;
constexpr std::size_t kUnavailableSearchPartyResultCount = 33;
constexpr std::size_t kSearchPartyResultCount = 34;
constexpr std::size_t kSearchPlayerResultCount = 38;

constexpr std::uint8_t kLfgLeaderRoleMask = 0x01u;
constexpr std::uint8_t kLfgTankRoleMask = 0x02u;
constexpr std::uint8_t kLfgHealerRoleMask = 0x04u;
constexpr std::uint8_t kLfgDamageRoleMask = 0x08u;
constexpr std::uint8_t kLfgCombatRoleMask =
    kLfgTankRoleMask | kLfgHealerRoleMask | kLfgDamageRoleMask;
constexpr std::uint32_t kLfgRoleCheckFreeAttempts = 2;
constexpr double kLfgRoleCheckResetSeconds = 10.0;
constexpr int kLfgRoleSelectionRequiredMessage = 0x2BD;

EventDispatcher *GetEvents(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKey);
  auto *mgr = static_cast<runtime::WorldUiRuntimeContext *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr ? &mgr->frame_events().dispatcher() : nullptr;
}

void FireLfgUpdateEvent(lua_State *L) {
  if (auto *dispatcher = GetEvents(L)) {
    dispatcher->FireEvent(openwow::ui::game::events::LFG_UPDATE);
  }
}

std::uint8_t GetLocalPlayerClassId(openwow::game::WorldSession *session) {
  if (!session) {
    return 0;
  }
  const auto *player = session->objects().GetLocalPlayerTyped();
  return player ? player->State().GetClass() : 0;
}

std::uint32_t GetLocalPlayerLevel(openwow::game::WorldSession *session) {
  if (!session) {
    return 0;
  }
  const auto *player = session->objects().GetLocalPlayerTyped();
  return player ? player->State().GetLevel() : 0;
}

std::uint32_t ResolveCurrentMapId(const openwow::game::WorldSession &session) {
  if (session.has_current_map()) {
    return session.current_map_id();
  }

  const auto world_state_map_id = session.world_states().map_id();
  if (world_state_map_id >= 0) {
    return static_cast<std::uint32_t>(world_state_map_id);
  }

  return session.objects().GetMapId();
}

std::uint32_t ResolveCurrentInstanceDifficultyIndex(
    const openwow::game::WorldSession &session) {
  return session.instance_difficulty().difficulty_index;
}

std::uint8_t FilterSelectedRolesForSession(openwow::game::WorldSession *session,
                                           std::uint8_t requested_roles) {
  const auto class_id = GetLocalPlayerClassId(session);
  return ::openwow::game::LFGSystem::FilterRolesForClass(class_id, requested_roles);
}

struct LfgRoleCheckThrottleState {
  openwow::core::IdaBurstThrottle throttle;

  [[nodiscard]] bool ConsumeToken(const double now_seconds) {
    return throttle.TryConsume(
        now_seconds, kLfgRoleCheckFreeAttempts, kLfgRoleCheckResetSeconds);
  }

  void Reset() { throttle.Reset(); }
};

LfgRoleCheckThrottleState &GetLfgRoleCheckThrottleState() {
  static LfgRoleCheckThrottleState throttle_state;
  return throttle_state;
}

bool ConsumeLfgRoleCheckThrottleToken() {
  return GetLfgRoleCheckThrottleState().ConsumeToken(
      openwow::core::GameClock::GetTickCountSeconds());
}

constexpr double kLfdLockInfoRequestResetSeconds = 10.0;

struct LfdLockInfoRequestThrottleState {
  openwow::core::IdaBurstThrottle throttle;

  [[nodiscard]] bool ConsumeToken(const double now_seconds) {
    return throttle.TryConsume(now_seconds, 2, kLfdLockInfoRequestResetSeconds);
  }

  void Reset() { throttle.Reset(); }
};

double DefaultLfdLockInfoRequestNowSeconds() {
  return openwow::core::GameClock::GetTickCountSeconds();
}

std::function<double()> &GetLfdLockInfoRequestClockFn() {
  static std::function<double()> clock_fn = DefaultLfdLockInfoRequestNowSeconds;
  return clock_fn;
}

double GetLfdLockInfoRequestNowSeconds() {
  const auto &clock_fn = GetLfdLockInfoRequestClockFn();
  return clock_fn ? clock_fn() : DefaultLfdLockInfoRequestNowSeconds();
}

LfdLockInfoRequestThrottleState &GetLfdPlayerLockInfoRequestThrottleState() {
  static LfdLockInfoRequestThrottleState throttle_state;
  return throttle_state;
}

LfdLockInfoRequestThrottleState &GetLfdPartyLockInfoRequestThrottleState() {
  static LfdLockInfoRequestThrottleState throttle_state;
  return throttle_state;
}

bool ConsumeLfdLockInfoRequestThrottleToken(LfdLockInfoRequestThrottleState &throttle_state) {
  return throttle_state.ConsumeToken(GetLfdLockInfoRequestNowSeconds());
}

void SendLfdLockInfoRequest(const openwow::net::wotlk::Opcode opcode) {
  (void)openwow::net::ClientServices__SendPacket(openwow::net::wotlk::WorldPacket(opcode));
}

int HandleLfdLockInfoRequest(lua_State *L, LfdLockInfoRequestThrottleState &throttle_state,
                             const openwow::net::wotlk::Opcode opcode) {
  if (!ConsumeLfdLockInfoRequestThrottleToken(throttle_state)) {
    lua_pushboolean(L, 0);
    return 1;
  }

  SendLfdLockInfoRequest(opcode);
  lua_pushboolean(L, 1);
  return 1;
}

bool PartyMemberStatsHasAuraSpellId(const openwow::game::PartyMemberStats &stats,
                                    const std::uint32_t spell_id) {
  return std::any_of(stats.auras.begin(), stats.auras.end(),
                     [spell_id](const openwow::game::GroupAuraInfo &aura) {
                       return aura.spell_id == spell_id;
                     });
}

bool TryQueryTrackedUnitStateAura(openwow::game::WorldSession *session,
                                  const openwow::game::ObjectGuid guid,
                                  const std::uint32_t spell_id,
                                  bool *out_has_aura) {
  if (session == nullptr || guid.IsEmpty() || out_has_aura == nullptr) {
    return false;
  }

  *out_has_aura = false;
  const auto raw_guid = guid.GetRawValue();
  if (openwow::game::GroupSystem::Get().GetMemberByGuid(raw_guid) != nullptr) {
    if (const auto cached = session->party_stats().GetCachedMember(raw_guid);
        cached.has_value()) {
      *out_has_aura = PartyMemberStatsHasAuraSpellId(cached->stats, spell_id);
    }
    return true;
  }

  if (session->battleground().HasArenaOpponentGuid(raw_guid)) {
    *out_has_aura = session->battleground().ArenaOpponentHasAura(raw_guid, spell_id);
    return true;
  }

  return false;
}

int LuaUnitHasTrackedAura(lua_State *L,
                          const char *usage,
                          const std::uint32_t spell_id) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, usage);
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const char *unit_id = lua_tostring(L, 1);
  const auto guid = ResolveUnitId(session, unit_id != nullptr ? unit_id : "");
  if (guid.IsEmpty()) {
    return 0;
  }

  if (const auto *object = session->objects().Get(guid);
      object != nullptr && object->IsUnit()) {
    lua_pushboolean(L,
                    session->aura().FindAuraBySpellId(guid.GetRawValue(), spell_id) != nullptr);
    return 1;
  }

  bool has_aura = false;
  if (!TryQueryTrackedUnitStateAura(session, guid, spell_id, &has_aura)) {
    return 0;
  }

  lua_pushboolean(L, has_aura);
  return 1;
}

std::string NormalizeLfgComment(const char *raw_comment) {
  if (raw_comment == nullptr) {
    return {};
  }

  std::string normalized;
  normalized.reserve(kLfgCommentMaxBytes);
  for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(raw_comment);
       *cursor != 0 && normalized.size() < kLfgCommentMaxBytes; ++cursor) {
    if (*cursor == '|') {
      continue;
    }
    normalized.push_back(static_cast<char>(*cursor));
  }

  for (std::size_t i = 0; i < normalized.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(normalized[i]);
    if (ch == '\r' || ch == '\n' ||
        (ch == '\\' && i + 1 < normalized.size() && normalized[i + 1] == 'n')) {
      normalized.resize(i);
      break;
    }
  }

  int lead_bytes = 0;
  for (std::size_t i = 0; i < normalized.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(normalized[i]);
    if ((ch & 0xC0u) == 0x80u) {
      continue;
    }

    ++lead_bytes;
    if (lead_bytes == kLfgCommentLeadByteCutoff) {
      normalized.resize(i);
      break;
    }
  }

  return normalized;
}

const openwow::data::dbc::DbcLoader *GetDbcLoaderLocal(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

std::uint8_t GetActiveLfgExpansionLevel() {
  return openwow::net::ClientServices::Instance().GetExpansionLevel();
}

std::uint32_t PackDungeonId(const openwow::data::dbc::LfgDungeonsEntry &dungeon);

bool ShouldUsePartyServerLfgInfo() {
  const auto &group_system = openwow::game::GroupSystem::Get();
  return group_system.GetTrackedPartyMemberCount() != 0 ||
         group_system.GetRealRaidMemberCount() != 0;
}

const openwow::game::LfgUpdateInfo *GetActiveServerLfgInfo(
    openwow::game::WorldSession *session, bool *using_party_update = nullptr) {
  if (using_party_update != nullptr) {
    *using_party_update = false;
  }

  if (session == nullptr) {
    return nullptr;
  }

  const auto &lfg = session->lfg();
  const bool use_party_update = ShouldUsePartyServerLfgInfo();
  if (use_party_update) {
    if (using_party_update != nullptr) {
      *using_party_update = true;
    }
    return lfg.party_update() ? &*lfg.party_update() : nullptr;
  }

  return lfg.player_update() ? &*lfg.player_update() : nullptr;
}

bool ShouldSendLfgLeave(openwow::game::WorldSession *session) {
  if (session == nullptr) {
    return false;
  }

  auto &lfg_system = ::openwow::game::LFGSystem::Get();
  if (lfg_system.HasActiveJoinRequest()) {
    return true;
  }

  const auto *server_info = GetActiveServerLfgInfo(session);
  return server_info != nullptr && server_info->has_extra;
}

const openwow::game::LfgUpdateInfo *GetActiveQueueStatsUpdate(
    openwow::game::WorldSession *session) {
  return GetActiveServerLfgInfo(session);
}

int FindLastQueueStatusIndexZeroBased(openwow::game::WorldSession *session) {
  if (session == nullptr) {
    return -1;
  }

  const auto *update = GetActiveServerLfgInfo(session);
  if (update == nullptr) {
    return -1;
  }

  const auto &queue_status = session->lfg().queue_status();
  if (!queue_status.has_value()) {
    return -1;
  }

  const auto it = std::find(update->dungeons.begin(), update->dungeons.end(),
                            queue_status->dungeon_id);
  if (it == update->dungeons.end()) {
    return -1;
  }

  return static_cast<int>(std::distance(update->dungeons.begin(), it));
}

const openwow::game::LfgRoleCheckUpdate *GetRoleCheckUpdate(
    openwow::game::WorldSession *session) {
  if (session == nullptr || !session->lfg().role_check()) {
    return nullptr;
  }
  return &*session->lfg().role_check();
}

const openwow::game::LfgRoleCheckPlayer *GetRoleCheckMemberByIndex(
    const openwow::game::LfgRoleCheckUpdate &role_check, std::size_t index) {
  if (index >= role_check.players.size()) {
    return nullptr;
  }
  return &role_check.players[index];
}

std::uint32_t ReadSaturatedU32Argument(lua_State *L, const int argument,
                                       const char *usage) {
  if (!lua_isnumber(L, argument)) {
    luaL_error(L, usage);
  }
  return openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, argument));
}

std::int32_t ReadSignedI32Argument(lua_State *L, const int argument,
                                   const char *usage) {
  if (!lua_isnumber(L, argument)) {
    luaL_error(L, usage);
  }
  return TruncateLuaNumberToSseI32(lua_tonumber(L, argument));
}

std::uint32_t ReadOneBasedSaturatedIndexArgument(lua_State *L, const char *usage) {
  return ReadSaturatedU32Argument(L, 1, usage) - 1u;
}

std::int32_t ReadOneBasedSignedIndexArgument(lua_State *L, const char *usage) {
  const auto one_based = ReadSignedI32Argument(L, 1, usage);
  return one_based == std::numeric_limits<std::int32_t>::min()
             ? std::numeric_limits<std::int32_t>::max()
             : one_based - 1;
}

void PushFrameScriptBoolean(lua_State *L, bool value) {
  lua_pushboolean(L, value ? 1 : 0);
}

void PushNilValues(lua_State *L, int count) {
  for (int i = 0; i < count; ++i) {
    lua_pushnil(L);
  }
}

std::string LookupAreaName(const openwow::data::dbc::DbcLoader *dbc, std::uint32_t area_id) {
  if (dbc != nullptr) {
    if (const auto *area = dbc->area_table().LookupEntry(area_id);
        area != nullptr && !area->name.empty()) {
      return std::string(area->name);
    }
  }
  return openwow::game::Localization::Get().GetString("UNKNOWN", "UNKNOWN");
}

const char *LfgRoleToken(std::uint32_t role_flags) {
  if ((role_flags & kLfgTankRoleMask) != 0) {
    return "TANK";
  }
  if ((role_flags & kLfgHealerRoleMask) != 0) {
    return "HEALER";
  }
  if ((role_flags & kLfgDamageRoleMask) != 0) {
    return "DAMAGER";
  }
  return "UNKNOWN";
}

std::optional<openwow::game::UnitQuerySnapshot>
ResolveSearchSnapshot(openwow::game::WorldSession *session, std::uint64_t guid) {
  if (session == nullptr || guid == 0)
    return std::nullopt;
  return openwow::game::UnitQueryBridge::Get().GetPlayerInfoByGUID(session, guid);
}

void PushLocalizedUnknown(lua_State *L) {
  const std::string unknown =
      openwow::game::Localization::Get().GetString("UNKNOWN", "UNKNOWN");
  lua_pushlstring(L, unknown.data(), unknown.size());
}

void PushSearchClassDisplay(lua_State *L,
                            const std::optional<openwow::game::UnitQuerySnapshot> &snapshot,
                            const bool empty_on_missing) {
  if (snapshot.has_value()) {
    const std::string_view class_name =
        LookupClassDisplayName(L, snapshot->classId, snapshot->genderId);
    if (!class_name.empty()) {
      lua_pushlstring(L, class_name.data(), class_name.size());
      return;
    }
  }

  if (empty_on_missing) {
    lua_pushstring(L, "");
    return;
  }

  PushLocalizedUnknown(L);
}

std::string FilterSearchComment(std::string_view raw_comment) {
  std::string filtered_comment(raw_comment);
  ::openwow::game::ChatFrame_MatureLanguageFilter(filtered_comment, false);
  return filtered_comment;
}

void PrepareOutputTable(lua_State *L) {
  if (lua_gettop(L) >= 1 && lua_istable(L, 1)) {
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
      lua_pop(L, 1);
      lua_pushvalue(L, -1);
      lua_pushnil(L);
      lua_rawset(L, 1);
    }
    lua_settop(L, 1);
    return;
  }

  lua_settop(L, 0);
  lua_newtable(L);
}

void CheckReusableOutputTableStack(lua_State *L, const char *function_name) {
  constexpr int kReusableTableScratchSlots = 3;
  constexpr int kWotlkLuaStackLimit = 2048;

  if (lua_gettop(L) > kWotlkLuaStackLimit - kReusableTableScratchSlots ||
      lua_checkstack(L, kReusableTableScratchSlots) == 0) {
    luaL_error(L, "stack overflow (%s)", function_name);
  }
}

void PrepareOptionalOutputTable(lua_State *L, const char *function_name, const char *usage) {
  const int type = lua_type(L, 1);
  if (type != LUA_TTABLE && type > 0) {
    luaL_error(L, "%s", usage);
  }

  if (type == LUA_TTABLE) {
    CheckReusableOutputTableStack(L, function_name);
  }

  PrepareOutputTable(L);
}

struct LfgChoiceEntry {
  std::int32_t id = 0;
  std::uint32_t group_key = 0;
};

bool IsLfdChoiceDungeon(const openwow::data::dbc::LfgDungeonsEntry &dungeon) {
  return (dungeon.flags & 0x2u) != 0 && dungeon.type_id != 6u &&
         (dungeon.flags & 0x4u) == 0;
}

bool IsLfrChoiceDungeon(const openwow::data::dbc::LfgDungeonsEntry &dungeon) {
  return dungeon.type_id == 2u;
}

bool ServerSnapshotHasSelectedDungeonType(const openwow::game::LfgUpdateInfo *update,
                                          const openwow::data::dbc::DbcLoader *dbc,
                                          const std::uint32_t type_id) {
  if (update == nullptr || dbc == nullptr) {
    return false;
  }

  for (const auto packed_dungeon_id : update->dungeons) {
    const auto *dungeon = dbc->lfg_dungeons().LookupEntry(packed_dungeon_id & 0x00FFFFFFu);
    if (dungeon != nullptr && dungeon->type_id == type_id) {
      return true;
    }
  }

  return false;
}

const openwow::data::dbc::LfgDungeonsEntry *LookupSelectableDungeonEntry(
    const openwow::data::dbc::DbcLoader *dbc, const std::uint32_t dungeon_id) {
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->lfg_dungeons().LookupEntry(dungeon_id);
}

std::string GetNormalizedPackedChoiceState(const char *cvar_name) {
  auto &cvars = CVarSystem::Instance();
  const auto current = cvars.GetCVar(cvar_name);
  const auto normalized = openwow::ui::game::NormalizePackedChoiceState(current);
  if (normalized != current) {
    cvars.SetCVar(cvar_name, normalized, true);
  }
  return normalized;
}

void UpdatePackedChoiceState(const char *cvar_name, const std::uint32_t key, const bool enabled) {
  auto &cvars = CVarSystem::Instance();
  const auto current = GetNormalizedPackedChoiceState(cvar_name);
  const auto result =
      openwow::ui::game::SetPackedChoiceStateBit(current, key, enabled);
  if (result != current) {
    cvars.SetCVar(cvar_name, result);
  }
}

void SetArchivedDungeonChoiceEnabled(const std::uint32_t dungeon_id, const bool enabled) {
  UpdatePackedChoiceState("lfdSelectedDungeons", dungeon_id, enabled);
}

void UpdateSelectedDungeonJoinState(const openwow::data::dbc::LfgDungeonsEntry &dungeon,
                                    const bool enabled) {
  auto &lfg = ::openwow::game::LFGSystem::Get();
  const auto packed_dungeon_id = PackDungeonId(dungeon);

  if (enabled) {
    lfg.AddSelectedDungeon(
        packed_dungeon_id, openwow::game::GroupSystem::Get().GetTrackedPartyMemberCount());
  } else {
    lfg.RemoveSelectedDungeon(packed_dungeon_id);
  }
}

void ClearAllSelectedDungeons() {
  ::openwow::game::LFGSystem::Get().ClearSelectedDungeons();
}

const openwow::data::dbc::LfgDungeonGroupEntry *LookupDungeonGroup(
    const openwow::data::dbc::DbcLoader *dbc, const std::uint32_t group_id) {
  if (dbc == nullptr) {
    return nullptr;
  }
  return dbc->lfg_dungeon_group().LookupEntry(group_id);
}

const openwow::data::dbc::MapDifficultyEntry *LookupMapDifficulty(
    const openwow::data::dbc::DbcLoader *dbc, const std::uint32_t map_id,
    const std::uint32_t difficulty) {
  return openwow::data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
}

void PushMapDifficultyMaxPlayers(lua_State *L,
                                 const openwow::data::dbc::MapDifficultyEntry *map_difficulty) {
  if (map_difficulty != nullptr) {
    lua_pushnumber(L, static_cast<lua_Number>(map_difficulty->max_players));
  } else {
    lua_pushnil(L);
  }
}

std::int32_t CompareLfgChoiceNameNoCase(std::string_view left, std::string_view right) {
  const std::string left_name(left);
  const std::string right_name(right);
  return openwow::core::SStrCmpUTF8NoCase(
      left_name.c_str(), right_name.c_str(), std::numeric_limits<std::size_t>::max());
}

std::uint32_t EffectiveChoiceLevelAverage(const openwow::data::dbc::DbcLoader *dbc,
                                          const openwow::data::dbc::LfgDungeonsEntry &entry) {
  const auto *override =
      openwow::data::DBClient_FindLfgDungeonExpansion(
          dbc, entry.id, GetActiveLfgExpansionLevel());
  const auto min_level = override != nullptr ? override->hard_level_min : entry.min_level;
  const auto max_level = override != nullptr ? override->hard_level_max : entry.max_level;
  return (min_level + max_level) / 2u;
}

std::vector<LfgChoiceEntry> BuildSortedChoiceEntries(
    const openwow::data::dbc::DbcLoader *dbc,
    const std::function<bool(const openwow::data::dbc::LfgDungeonsEntry &)> &include_dungeon,
    const std::function<bool(const openwow::data::dbc::LfgDungeonGroupEntry &)> &include_group) {
  std::vector<LfgChoiceEntry> entries;
  if (dbc == nullptr) {
    return entries;
  }

  for (const auto &dungeon : dbc->lfg_dungeons()) {
    if (!include_dungeon(dungeon)) {
      continue;
    }
    entries.push_back({static_cast<std::int32_t>(dungeon.id), dungeon.group_id});
  }

  for (const auto &group : dbc->lfg_dungeon_group()) {
    if (!include_group(group)) {
      continue;
    }
    entries.push_back({-static_cast<std::int32_t>(group.id), group.parent_group_id});
  }

  std::sort(entries.begin(), entries.end(),
            [dbc](const LfgChoiceEntry &left, const LfgChoiceEntry &right) {
              const auto left_group_key =
                  left.id < 0 ? static_cast<std::uint32_t>(-left.id) : left.group_key;
              const auto right_group_key =
                  right.id < 0 ? static_cast<std::uint32_t>(-right.id) : right.group_key;

              const auto *left_group = LookupDungeonGroup(dbc, left_group_key);
              const auto *right_group = LookupDungeonGroup(dbc, right_group_key);
              const auto left_group_order = left_group != nullptr ? left_group->order_index : 0u;
              const auto right_group_order = right_group != nullptr ? right_group->order_index : 0u;
              if (left_group_order != right_group_order) {
                return left_group_order < right_group_order;
              }

              if ((left.id < 0) != (right.id < 0)) {
                return left.id < 0;
              }

              if (left.id < 0) {
                return false;
              }

              const auto *left_dungeon =
                  dbc != nullptr ? dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(left.id))
                                 : nullptr;
              const auto *right_dungeon =
                  dbc != nullptr
                      ? dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(right.id))
                      : nullptr;
              const auto left_order = left_dungeon != nullptr ? left_dungeon->order_index : 0u;
              const auto right_order = right_dungeon != nullptr ? right_dungeon->order_index : 0u;
              if (left_order != right_order) {
                return left_order < right_order;
              }

              const auto left_level_average =
                  left_dungeon != nullptr ? EffectiveChoiceLevelAverage(dbc, *left_dungeon) : 0u;
              const auto right_level_average =
                  right_dungeon != nullptr ? EffectiveChoiceLevelAverage(dbc, *right_dungeon) : 0u;
              if (left_level_average != right_level_average) {
                return left_level_average < right_level_average;
              }

              return CompareLfgChoiceNameNoCase(
                         left_dungeon != nullptr ? left_dungeon->name : std::string_view{},
                         right_dungeon != nullptr ? right_dungeon->name : std::string_view{}) < 0;
            });
  return entries;
}

std::vector<LfgChoiceEntry> BuildLfdChoiceEntries(const openwow::data::dbc::DbcLoader *dbc) {
  return BuildSortedChoiceEntries(
      dbc, IsLfdChoiceDungeon,
      [](const openwow::data::dbc::LfgDungeonGroupEntry &group) { return group.type_id != 0u; });
}

std::vector<LfgChoiceEntry> BuildLfrChoiceEntries(const openwow::data::dbc::DbcLoader *dbc) {
  return BuildSortedChoiceEntries(
      dbc, IsLfrChoiceDungeon,
      [](const openwow::data::dbc::LfgDungeonGroupEntry &) { return true; });
}

std::vector<std::uint32_t> NormalizeSelectedDungeonIds(
    const openwow::data::dbc::DbcLoader *dbc, const std::vector<std::uint32_t> &dungeons) {
  std::vector<std::uint32_t> normalized;
  normalized.reserve(dungeons.size());
  for (const auto dungeon_id : dungeons) {
    if ((dungeon_id & 0xFF000000u) != 0u) {
      normalized.push_back(dungeon_id);
      continue;
    }

    if (dbc == nullptr) {
      normalized.push_back(dungeon_id);
      continue;
    }

    if (const auto *dungeon = dbc->lfg_dungeons().LookupEntry(dungeon_id); dungeon != nullptr) {
      normalized.push_back(PackDungeonId(*dungeon));
    } else {
      normalized.push_back(dungeon_id);
    }
  }
  return normalized;
}

bool TrySendLfgJoin(lua_State *L, openwow::game::WorldSession &session,
                    const std::vector<std::uint32_t> &dungeons,
                    const bool emit_empty_selection_message) {
  if (dungeons.empty()) {
    if (emit_empty_selection_message) {
      DisplaySystemMessage(700);
    }
    return false;
  }

  auto &lfg = ::openwow::game::LFGSystem::Get();
  if (!lfg.TryBeginJoinRequest()) {
    return false;
  }

  const auto roles = FilterSelectedRolesForSession(&session, lfg.GetRoles());
  if ((roles & kLfgCombatRoleMask) == 0) {
    DisplaySystemMessage(kLfgRoleSelectionRequiredMessage);
    return false;
  }

  const auto normalized_dungeons = NormalizeSelectedDungeonIds(GetDbcLoaderLocal(L), dungeons);
  session.interaction().SendLfgJoin(roles, normalized_dungeons, lfg.GetComment());
  lfg.JoinQueue(dungeons, roles);
  return true;
}

void PushLfgChoiceInfoTable(lua_State *L, const openwow::data::dbc::DbcLoader *dbc,
                            const std::int32_t entry_id) {
  if (dbc == nullptr) {
    return;
  }

  if (entry_id < 0) {
    const auto *group = dbc->lfg_dungeon_group().LookupEntry(static_cast<std::uint32_t>(-entry_id));
    if (group == nullptr) {
      return;
    }

    lua_pushnumber(L, 1.0);
    lua_pushstring(L, group->name.empty() ? "" : std::string(group->name).c_str());
    lua_rawset(L, -3);

    lua_pushnumber(L, 2.0);
    lua_pushnumber(L, static_cast<lua_Number>(group->type_id));
    lua_rawset(L, -3);
    return;
  }

  const auto *dungeon = dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(entry_id));
  if (dungeon == nullptr) {
    return;
  }

  const auto *override =
      openwow::data::DBClient_FindLfgDungeonExpansion(
          dbc, dungeon->id, GetActiveLfgExpansionLevel());
  const auto min_level = override != nullptr ? override->hard_level_min : dungeon->min_level;
  const auto max_level = override != nullptr ? override->hard_level_max : dungeon->max_level;
  const auto min_rec_level =
      override != nullptr ? override->target_level_min : dungeon->rec_min_level;
  const auto max_rec_level =
      override != nullptr ? override->target_level_max : dungeon->rec_max_level;
  const auto *map_difficulty = LookupMapDifficulty(dbc, dungeon->map_id, dungeon->difficulty);

  lua_pushnumber(L, 1.0);
  lua_pushstring(L, dungeon->name.empty() ? "" : std::string(dungeon->name).c_str());
  lua_rawset(L, -3);

  lua_pushnumber(L, 2.0);
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->type_id));
  lua_rawset(L, -3);

  lua_pushnumber(L, 3.0);
  lua_pushnumber(L, static_cast<lua_Number>(min_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 4.0);
  lua_pushnumber(L, static_cast<lua_Number>(max_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 5.0);
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->rec_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 6.0);
  lua_pushnumber(L, static_cast<lua_Number>(min_rec_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 7.0);
  lua_pushnumber(L, static_cast<lua_Number>(max_rec_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 8.0);
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->expansion_level));
  lua_rawset(L, -3);

  lua_pushnumber(L, 9.0);
  lua_pushnumber(L, static_cast<lua_Number>(-static_cast<std::int32_t>(dungeon->group_id)));
  lua_rawset(L, -3);

  lua_pushnumber(L, 10.0);
  lua_pushstring(L, dungeon->texture_filename.empty() ? ""
                                                      : std::string(dungeon->texture_filename).c_str());
  lua_rawset(L, -3);

  lua_pushnumber(L, 11.0);
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->difficulty));
  lua_rawset(L, -3);

  lua_pushnumber(L, 12.0);
  PushMapDifficultyMaxPlayers(L, map_difficulty);
  lua_rawset(L, -3);

  lua_pushnumber(L, 13.0);
  lua_pushstring(L, dungeon->description.empty() ? "" : std::string(dungeon->description).c_str());
  lua_rawset(L, -3);

  lua_pushnumber(L, 14.0);
  PushFrameScriptBoolean(L, (dungeon->flags & 0x4u) != 0);
  lua_rawset(L, -3);
}

struct SearchDisplayEntry {
  const openwow::game::LfgSearchGroupResult *group = nullptr;
  const openwow::game::LfgSearchPlayerResult *player = nullptr;
};

int CompareAsciiNoCase(const std::string &left, const std::string &right) {
  const auto to_lower = [](unsigned char ch) -> unsigned char {
    return static_cast<unsigned char>(std::tolower(ch));
  };

  const auto count = std::min(left.size(), right.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto lhs = to_lower(static_cast<unsigned char>(left[i]));
    const auto rhs = to_lower(static_cast<unsigned char>(right[i]));
    if (lhs < rhs)
      return -1;
    if (lhs > rhs)
      return 1;
  }
  if (left.size() < right.size())
    return -1;
  if (left.size() > right.size())
    return 1;
  return 0;
}

bool EqualsAsciiNoCase(const std::string &left, const char *right) {
  return CompareAsciiNoCase(left, right != nullptr ? std::string(right) : std::string()) == 0;
}

std::vector<SearchDisplayEntry> BuildSearchDisplay(openwow::game::WorldSession *session,
                                                   const openwow::game::LfgManager &manager,
                                                   const openwow::data::dbc::DbcLoader *dbc) {
  (void)session;
  (void)dbc;
  auto groups = manager.SearchGroups();
  auto players = manager.StandaloneSearchPlayers();

  std::vector<SearchDisplayEntry> display;
  display.reserve(groups.size() + players.size());

  const auto append_groups = [&]() {
    for (const auto *group : groups) {
      display.push_back({group, manager.GetSearchPrimaryPlayer(group->guid)});
    }
  };
  const auto append_players = [&]() {
    for (const auto *player : players) {
      display.push_back({nullptr, player});
    }
  };

  if (manager.search_players_first()) {
    append_players();
    append_groups();
  } else {
    append_groups();
    append_players();
  }

  return display;
}

const SearchDisplayEntry *FindSearchDisplayEntry(openwow::game::WorldSession *session,
                                                 const openwow::game::LfgManager &manager,
                                                 const openwow::data::dbc::DbcLoader *dbc,
                                                 std::size_t one_based_index,
                                                 std::vector<SearchDisplayEntry> &scratch) {
  scratch = BuildSearchDisplay(session, manager, dbc);
  if (one_based_index == 0 || one_based_index > scratch.size())
    return nullptr;
  return &scratch[one_based_index - 1];
}

const openwow::data::dbc::LfgDungeonsEntry *
LookupActiveSearchDungeon(const openwow::game::LfgManager &manager,
                          const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr || !manager.has_search_results())
    return nullptr;
  return dbc->lfg_dungeons().LookupEntry(manager.active_search_id() & 0x00FFFFFFu);
}

const openwow::data::dbc::LfgDungeonsEntry *
LookupProposalDungeon(const openwow::game::LfgManager &manager,
                      const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr || !manager.proposal()) {
    return nullptr;
  }
  return dbc->lfg_dungeons().LookupEntry(manager.proposal()->dungeon_entry & 0x00FFFFFFu);
}

std::pair<std::uint32_t, std::uint32_t>
CountDungeonEncountersForEntry(const openwow::data::dbc::DbcLoader *dbc,
                               const openwow::data::dbc::LfgDungeonsEntry *dungeon,
                               std::uint32_t completion_mask) {
  if (dbc == nullptr || dungeon == nullptr) {
    return {0, 0};
  }

  if (dungeon->type_id == 6u) {
    return {0, completion_mask != 0 ? 1u : 0u};
  }

  if ((dungeon->flags & 0x4u) != 0) {
    return {1, completion_mask != 0 ? 1u : 0u};
  }

  std::uint32_t total = 0;
  std::uint32_t completed = 0;
  for (const auto &encounter : dbc->dungeon_encounter()) {
    if (encounter.map_id != dungeon->map_id || encounter.difficulty != dungeon->difficulty) {
      continue;
    }

    ++total;
    if ((completion_mask & (1u << encounter.bit)) != 0) {
      ++completed;
    }
  }

  return {total, completed};
}

std::pair<std::uint32_t, std::uint32_t>
CountDungeonEncounters(const openwow::game::LfgManager &manager,
                       const openwow::data::dbc::DbcLoader *dbc, std::uint32_t completion_mask) {
  return CountDungeonEncountersForEntry(dbc, LookupActiveSearchDungeon(manager, dbc),
                                        completion_mask);
}

std::uint32_t PackDungeonSelectionId(const std::uint32_t type_id,
                                     const std::uint32_t dungeon_id) {
  return (dungeon_id & 0x00FFFFFFu) | (type_id << 24);
}

std::uint32_t PackDungeonId(const openwow::data::dbc::LfgDungeonsEntry &dungeon) {
  return PackDungeonSelectionId(dungeon.type_id, dungeon.id);
}

int PushLfgDungeonInfo(lua_State *L, const openwow::data::dbc::DbcLoader *dbc,
                       const std::uint32_t dungeon_id) {
  if (dbc == nullptr) {
    return 0;
  }

  const auto *dungeon = dbc->lfg_dungeons().LookupEntry(dungeon_id);
  if (dungeon == nullptr) {
    return 0;
  }

  const auto *override =
      openwow::data::DBClient_FindLfgDungeonExpansion(
          dbc, dungeon_id, GetActiveLfgExpansionLevel());
  const auto min_level = override != nullptr ? override->hard_level_min : dungeon->min_level;
  const auto max_level = override != nullptr ? override->hard_level_max : dungeon->max_level;
  const auto min_rec_level =
      override != nullptr ? override->target_level_min : dungeon->rec_min_level;
  const auto max_rec_level =
      override != nullptr ? override->target_level_max : dungeon->rec_max_level;
  const auto *map_difficulty = LookupMapDifficulty(dbc, dungeon->map_id, dungeon->difficulty);

  lua_pushstring(L, dungeon->name.empty() ? "" : std::string(dungeon->name).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->type_id));
  lua_pushnumber(L, static_cast<lua_Number>(min_level));
  lua_pushnumber(L, static_cast<lua_Number>(max_level));
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->rec_level));
  lua_pushnumber(L, static_cast<lua_Number>(min_rec_level));
  lua_pushnumber(L, static_cast<lua_Number>(max_rec_level));
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->expansion_level));
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->group_id));
  lua_pushstring(L, dungeon->texture_filename.empty() ? ""
                                                      : std::string(dungeon->texture_filename).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->difficulty));
  PushMapDifficultyMaxPlayers(L, map_difficulty);
  lua_pushstring(L, dungeon->description.empty() ? ""
                                                 : std::string(dungeon->description).c_str());
  PushFrameScriptBoolean(L, (dungeon->flags & 0x4u) != 0);
  return 14;
}

int PushDungeonEncounter(lua_State *L, const openwow::data::dbc::DbcLoader *dbc,
                         const openwow::data::dbc::LfgDungeonsEntry *dungeon,
                         std::uint32_t encounter_index, std::uint32_t completion_mask) {
  if (dbc == nullptr || dungeon == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  std::uint32_t current = 0;
  for (const auto &encounter : dbc->dungeon_encounter()) {
    if (encounter.map_id != dungeon->map_id || encounter.difficulty != dungeon->difficulty) {
      continue;
    }
    if (current++ != encounter_index) {
      continue;
    }

    lua_pushstring(L, encounter.name.empty() ? "" : std::string(encounter.name).c_str());
    if (const auto *icon = dbc->spell_icon().LookupEntry(encounter.spell_icon_id);
        icon != nullptr && !icon->icon_path.empty()) {
      lua_pushstring(L, std::string(icon->icon_path).c_str());
    } else {
      lua_pushnil(L);
    }
    PushFrameScriptBoolean(L, (completion_mask & (1u << encounter.bit)) != 0);
    return 3;
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  return 3;
}

int PushSearchEncounter(lua_State *L, const openwow::game::LfgManager &manager,
                        const openwow::data::dbc::DbcLoader *dbc, std::uint32_t encounter_index,
                        std::uint32_t completion_mask) {
  return PushDungeonEncounter(L, dbc, LookupActiveSearchDungeon(manager, dbc), encounter_index,
                              completion_mask);
}

std::uint32_t TruncateFloatToUint(float value) {
  return value > 0.0f ? static_cast<std::uint32_t>(value) : 0;
}

int PushSearchExtendedFields(lua_State *L, const openwow::game::LfgSearchPlayerResult *player) {
  if (player == nullptr) {
    for (std::size_t i = 0; i < kSearchExtendedResultCount; ++i)
      lua_pushnil(L);
    return static_cast<int>(kSearchExtendedResultCount);
  }

  lua_pushwowbool(L, (player->search_flags & 0x01) != 0);
  lua_pushwowbool(L, (player->search_flags & 0x02) != 0);
  lua_pushwowbool(L, (player->search_flags & 0x04) != 0);
  lua_pushwowbool(L, (player->search_flags & 0x08) != 0);
  lua_pushnumber(L, static_cast<lua_Number>(player->raw_u32_84_100[4]));
  lua_pushnumber(L, static_cast<lua_Number>(player->raw_u8_47_49[0]));
  lua_pushnumber(L, static_cast<lua_Number>(player->raw_u8_47_49[1]));
  lua_pushnumber(L, static_cast<lua_Number>(player->raw_u8_47_49[2]));
  lua_pushwowbool(L, player->joined_group);
  for (const auto value : player->raw_u32_52_72) {
    lua_pushnumber(L, static_cast<lua_Number>(value));
  }
  lua_pushnumber(L, static_cast<lua_Number>(TruncateFloatToUint(player->raw_f32_76_80[0])));
  lua_pushnumber(L, static_cast<lua_Number>(TruncateFloatToUint(player->raw_f32_76_80[1])));
  for (std::size_t i = 0; i < 4; ++i) {
    lua_pushnumber(L, static_cast<lua_Number>(player->raw_u32_84_100[i]));
  }
  lua_pushnumber(L, static_cast<lua_Number>(TruncateFloatToUint(player->raw_f32_104)));
  for (const auto value : player->raw_u32_108_128) {
    lua_pushnumber(L, static_cast<lua_Number>(value));
  }
  return static_cast<int>(kSearchExtendedResultCount);
}

int PushUnavailableSearchPartyResult(lua_State *L) {
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kUnavailableSearchPartyResultCount,
      "unavailable LFG search party values");
  lua_pushnil(L);
  lua_pushstring(L, "");
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnil(L);
  (void)PushSearchExtendedFields(L, nullptr);
  return result_count;
}

std::string ResolveLfgRewardTexturePath(const openwow::data::dbc::DbcLoader *dbc,
                                        const std::uint32_t display_info_id) {
  if (dbc != nullptr && display_info_id != 0) {
    if (const auto *display = dbc->item_display_info().LookupEntry(display_info_id);
        display != nullptr && !std::string_view(display->inventory_icon).empty()) {
      return BuildItemIconTexturePath(display->inventory_icon);
    }
  }

  return BuildItemIconTexturePath(kFallbackItemIconName);
}

void FireLfgUpdateRandomInfoEvent() {
  if (auto *ui = runtime::WorldUiRuntimeContext::FromActiveLua()) {
    ui->frame_events().dispatcher().FireEvent(
        openwow::ui::game::events::LFG_UPDATE_RANDOM_INFO);
  }
}

const openwow::game::LfgRewardItem *FindRandomDungeonRewardItem(
    openwow::game::WorldSession *session, const std::uint32_t dungeon_id,
    const int reward_index) {
  if (session == nullptr || reward_index < 0) {
    return nullptr;
  }

  constexpr std::uint32_t kRandomDungeonType = 6u << 24;
  const auto packed_dungeon_id = kRandomDungeonType | (dungeon_id & 0x00FFFFFFu);
  const auto *state = session->lfg().FindPlayerDungeonState(packed_dungeon_id);
  if (state == nullptr || static_cast<std::size_t>(reward_index) >= state->rewards.size()) {
    return nullptr;
  }

  return &state->rewards[static_cast<std::size_t>(reward_index)];
}

const openwow::game::LfgPlayerReward *GetCompletionRewardState(
    openwow::game::WorldSession *session, const openwow::data::dbc::DbcLoader *dbc,
    const openwow::data::dbc::LfgDungeonsEntry **out_dungeon = nullptr) {
  if (out_dungeon != nullptr) {
    *out_dungeon = nullptr;
  }

  if (session == nullptr || dbc == nullptr || !session->lfg().player_reward().has_value()) {
    return nullptr;
  }

  const auto &reward = *session->lfg().player_reward();
  const auto completed_dungeon_id = reward.completed_dungeon_entry & kPackedDungeonIdMask;
  if (completed_dungeon_id == 0) {
    return nullptr;
  }

  const auto *dungeon = dbc->lfg_dungeons().LookupEntry(completed_dungeon_id);
  if (dungeon == nullptr) {
    return nullptr;
  }

  if (out_dungeon != nullptr) {
    *out_dungeon = dungeon;
  }
  return &reward;
}

}

void SetLfdLockInfoRequestClockForTests(std::function<double()> clock_fn) {
  GetLfdLockInfoRequestClockFn() =
      clock_fn ? std::move(clock_fn) : DefaultLfdLockInfoRequestNowSeconds;
}

void ResetLfgRoleCheckThrottleForTests() {
  GetLfgRoleCheckThrottleState().Reset();
}

void ResetLfdLockInfoRequestThrottleStateForTests() {
  ResetLfgRoleCheckThrottleForTests();
  GetLfdLockInfoRequestClockFn() = DefaultLfdLockInfoRequestNowSeconds;
  GetLfdPlayerLockInfoRequestThrottleState().Reset();
  GetLfdPartyLockInfoRequestThrottleState().Reset();
}

int LuaGetLFGQueueStats(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *update = GetActiveQueueStatsUpdate(session);
  if (update == nullptr || !update->has_extra || !update->queued) {
    return 0;
  }

  const auto &queue_status = session->lfg().queue_status();
  if (!queue_status.has_value() || queue_status->dungeon_id == 0) {
    return 0;
  }

  lua_pushboolean(L, 1);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->tanks_needed));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->healers_needed));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->dps_needed));

  const auto *dbc = GetDbcLoaderLocal(L);
  const auto *dungeon = dbc != nullptr
      ? dbc->lfg_dungeons().LookupEntry(queue_status->dungeon_id)
      : nullptr;
  if (dungeon == nullptr) {
    PushNilValues(L, 8);
    return 13;
  }

  lua_pushnumber(L, static_cast<lua_Number>(dungeon->type_id));
  lua_pushlstring(L, dungeon->name.data(), dungeon->name.size());
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->wait_time));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->wait_time_tank));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->wait_time_healer));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->wait_time_dps));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->wait_time_avg));
  lua_pushnumber(L, static_cast<lua_Number>(queue_status->queued_time) * 0.001);
  return 13;
}

int LuaGetLFGDungeonInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetLFGDungeonInfo(dungeonID)");
  }

  const auto dungeon_id = static_cast<std::uint32_t>(lua_tonumber(L, 1));
  return PushLfgDungeonInfo(L, GetDbcLoaderLocal(L), dungeon_id);
}

int LuaGetLFGDungeonRewards(lua_State *L) {
  const auto *dbc = GetDbcLoaderLocal(L);
  constexpr auto kUsage = "Usage: GetLFGDungeonRewards(dungeonID)";
  const auto dungeon_id = ReadSignedI32Argument(L, 1, kUsage);

  if (dbc == nullptr || dungeon_id < 0) {
    return 0;
  }

  const auto *dungeon =
      dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(dungeon_id));
  if (dungeon == nullptr) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto packed_dungeon_id = PackDungeonId(*dungeon);
  const auto *state =
      session != nullptr ? session->lfg().FindPlayerDungeonState(packed_dungeon_id) : nullptr;

  lua_pushboolean(L, state != nullptr && state->reward_done);
  lua_pushnumber(L, static_cast<lua_Number>(state != nullptr ? state->reward_money : 0u));
  lua_pushnumber(L, static_cast<lua_Number>(state != nullptr ? state->reward_money_var : 0u));
  lua_pushnumber(L, static_cast<lua_Number>(state != nullptr ? state->reward_xp : 0u));
  lua_pushnumber(L, static_cast<lua_Number>(state != nullptr ? state->reward_xp_var : 0u));
  lua_pushnumber(L, static_cast<lua_Number>(state != nullptr ? state->rewards.size() : 0u));
  return 6;
}

int LuaGetLFGDungeonRewardInfo(lua_State *L) {
  constexpr auto kUsage = "Usage: GetLFGDungeonRewardInfo(dungeonID, lootIndex)";
  const auto dungeon_id = ReadSignedI32Argument(L, 1, kUsage);
  const auto reward_number = ReadSignedI32Argument(L, 2, kUsage);
  if (dungeon_id < 0 || reward_number <= 0) {
    return 0;
  }
  const auto reward_index = static_cast<std::size_t>(reward_number - 1);

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  const auto *dungeon =
      dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(dungeon_id));
  if (dungeon == nullptr) {
    return 0;
  }

  if (dungeon->type_id != 6u && (dungeon->flags & 0x4u) == 0) {
    return 0;
  }

  const auto *state = session->lfg().FindPlayerDungeonState(PackDungeonId(*dungeon));
  if (state == nullptr || reward_index >= state->rewards.size()) {
    return 0;
  }

  const auto &reward = state->rewards[reward_index];
  std::string item_name;
  if (const auto *item = session->query_cache().GetOrRequestItemTemplate(
          reward.item_id,
          {.dedupe_callbacks = false,
           .callback = [session, item_id = reward.item_id](const bool success) {
             if (!success || session->query_cache().GetItemTemplate(item_id) == nullptr) {
               return;
             }
             FireLfgUpdateRandomInfoEvent();
           }});
      item != nullptr) {
    item_name = item->name;
  }

  const auto texture_path = ResolveLfgRewardTexturePath(dbc, reward.display_info_id);
  lua_pushstring(L, item_name.c_str());
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(reward.item_count));
  return 3;
}

int LuaGetLFGDungeonRewardLink(lua_State *L) {
  constexpr auto kUsage = "Usage: GetLFGDungeonRewardLink(dungeonID, lootIndex)";
  const auto dungeon_id = ReadSignedI32Argument(L, 1, kUsage);
  const auto reward_number = ReadSignedI32Argument(L, 2, kUsage);
  if (dungeon_id < 0 || reward_number <= 0) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto reward_index = reward_number - 1;
  const auto *reward = FindRandomDungeonRewardItem(
      session, static_cast<std::uint32_t>(dungeon_id), reward_index);
  if (reward == nullptr) {
    return 0;
  }

  const auto *item_template = session->query_cache().GetOrRequestItemTemplate(reward->item_id);
  if (item_template == nullptr) {
    return 0;
  }

  const auto link = BuildLootItemLink(
      reward->item_id, static_cast<std::uint8_t>(item_template->quality), item_template->name, 0,
      0, GetLocalPlayerLevel(session));
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetLFGProposal(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr) {
    return 0;
  }

  const auto &manager = session->lfg();
  if (!manager.proposal()) {
    return 0;
  }

  const auto *dungeon = LookupProposalDungeon(manager, dbc);
  if (dungeon == nullptr) {
    return 0;
  }

  const auto &proposal = *manager.proposal();
  const auto current_player = std::find_if(
      proposal.players.begin(), proposal.players.end(),
      [](const openwow::game::LfgProposalPlayer &player) { return player.is_current_player; });
  const auto has_current_player = current_player != proposal.players.end();
  const auto current_role_flags = has_current_player ? current_player->role : 0u;
  const auto [total_encounters, completed_encounters] =
      CountDungeonEncountersForEntry(dbc, dungeon, proposal.encounter_mask);

  PushFrameScriptBoolean(L, true);
  lua_pushnumber(L, static_cast<lua_Number>(proposal.dungeon_entry >> 24));
  lua_pushnumber(L, static_cast<lua_Number>(proposal.dungeon_entry & 0x00FFFFFFu));
  lua_pushstring(L, dungeon->name.empty() ? "" : std::string(dungeon->name).c_str());
  lua_pushstring(L,
                 dungeon->texture_filename.empty() ? ""
                                                   : std::string(dungeon->texture_filename).c_str());
  lua_pushstring(L, LfgRoleToken(current_role_flags));
  PushFrameScriptBoolean(L, has_current_player && current_player->has_answered);
  lua_pushnumber(L, static_cast<lua_Number>(total_encounters));
  lua_pushnumber(L, static_cast<lua_Number>(completed_encounters));
  lua_pushnumber(L, 5.0);
  PushFrameScriptBoolean(L, (current_role_flags & kLfgLeaderRoleMask) != 0);
  PushFrameScriptBoolean(L, (dungeon->flags & 0x4u) != 0);

  return 12;
}

int LuaGetLFGCompletionReward(lua_State *L) {
  const openwow::data::dbc::LfgDungeonsEntry *dungeon = nullptr;
  const auto *reward = GetCompletionRewardState(GetWorldSession(L), GetDbcLoaderLocal(L), &dungeon);
  if (reward == nullptr || dungeon == nullptr) {
    return 0;
  }

  lua_pushlstring(L, dungeon->name.data(), dungeon->name.size());
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->type_id));
  lua_pushlstring(L, dungeon->texture_filename.data(), dungeon->texture_filename.size());
  lua_pushnumber(L, static_cast<lua_Number>(reward->base_money_reward));
  lua_pushnumber(L, static_cast<lua_Number>(reward->base_xp_reward));
  lua_pushnumber(L, static_cast<lua_Number>(reward->variable_money_reward));
  lua_pushnumber(L, static_cast<lua_Number>(reward->variable_xp_reward));
  lua_pushnumber(L, static_cast<lua_Number>(reward->strangers_count));
  lua_pushnumber(L, static_cast<lua_Number>(reward->items.size()));
  return 9;
}

int LuaCompleteLFGRoleCheck(lua_State *L) {
  if (!ConsumeLfgRoleCheckThrottleToken()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto &lfg = ::openwow::game::LFGSystem::Get();
  std::uint8_t roles = 0;

  if (ScriptReadBoolArgOrDefault(L, 1, false)) {
    roles = FilterSelectedRolesForSession(session, lfg.GetRoles());
    if ((roles & kLfgCombatRoleMask) == 0) {
      DisplaySystemMessage(kLfgRoleSelectionRequiredMessage);
      lua_pushwowbool(L, false);
      return 1;
    }
  }

  session->interaction().SendLfgSetRoles(roles);
  lua_pushwowbool(L, true);
  return 1;
}

int LuaSetLFGRoles(lua_State *L) {
  const bool leader = ScriptReadBoolArgOrDefault(L, 1, false);
  const bool tank = ScriptReadBoolArgOrDefault(L, 2, false);
  const bool healer = ScriptReadBoolArgOrDefault(L, 3, false);
  const bool damage = ScriptReadBoolArgOrDefault(L, 4, false);
  std::uint8_t mask = 0;
  if (leader)
    mask |= kLfgLeaderRoleMask;
  if (tank)
    mask |= kLfgTankRoleMask;
  if (healer)
    mask |= kLfgHealerRoleMask;
  if (damage)
    mask |= kLfgDamageRoleMask;

  auto *session = GetWorldSession(L);
  const auto filtered_mask = FilterSelectedRolesForSession(session, mask);
  auto &lfg = ::openwow::game::LFGSystem::Get();
  lfg.SetRoles(filtered_mask);

  if (auto *dispatcher = GetEvents(L)) {
    dispatcher->FireEvent(openwow::ui::game::events::LFG_ROLE_UPDATE);
  }

  return 0;
}

int LuaGetAvailableRoles(lua_State *L) {
  const auto class_id = GetLocalPlayerClassId(GetWorldSession(L));
  const auto mask = ::openwow::game::LFGSystem::GetRoleAvailabilityMaskForClass(class_id);

  lua_pushwowbool(L, (mask & kLfgTankRoleMask) != 0);
  lua_pushwowbool(L, (mask & kLfgHealerRoleMask) != 0);
  lua_pushwowbool(L, (mask & kLfgDamageRoleMask) != 0);
  return 3;
}

int LuaGetLFGRoles(lua_State *L) {
  if (GetWorldSession(L) == nullptr) {
    PushNilValues(L, 4);
    return 4;
  }

  const std::uint8_t mask = ::openwow::game::LFGSystem::Get().GetRoles();
  FrameScript_PushBoolean(L, (mask & kLfgLeaderRoleMask) != 0);
  FrameScript_PushBoolean(L, (mask & kLfgTankRoleMask) != 0);
  FrameScript_PushBoolean(L, (mask & kLfgHealerRoleMask) != 0);
  FrameScript_PushBoolean(L, (mask & kLfgDamageRoleMask) != 0);
  return 4;
}

int LuaJoinLFG(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  TrySendLfgJoin(L, *session, ::openwow::game::LFGSystem::Get().GetSelectedDungeons(), true);
  return 0;
}

int LuaLeaveLFG(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session || !ShouldSendLfgLeave(session))
    return 0;
  ::openwow::game::LFGSystem::Get().LeaveQueue();
  session->interaction().SendLfgLeave();
  return 0;
}

int LuaGetLFGRandomDungeonInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetLFGRandomDungeonInfo(index)");
  }

  const auto index = static_cast<std::int32_t>(lua_tonumber(L, 1));
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr || index <= 0) {
    return 0;
  }

  const auto dungeon_ids = session->lfg().GetAvailableRandomDungeonIds(*dbc);
  const auto position = static_cast<std::size_t>(index - 1);
  if (position >= dungeon_ids.size()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(dungeon_ids[position]));
  return 1 + PushLfgDungeonInfo(L, dbc, dungeon_ids[position]);
}

int LuaGetNumRandomDungeons(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto count = session != nullptr && dbc != nullptr
                         ? session->lfg().GetAvailableRandomDungeonIds(*dbc).size()
                         : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaSetLFGDungeon(lua_State *L) {
  const auto dungeon_id =
      ReadSaturatedU32Argument(L, 1, "Usage: SetLFGDungeon(type)");
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto *dungeon = LookupSelectableDungeonEntry(dbc, dungeon_id);
  if (dungeon == nullptr) {
    return 0;
  }

  UpdateSelectedDungeonJoinState(*dungeon, true);
  return 0;
}

int LuaClearAllLFGDungeons([[maybe_unused]] lua_State *L) {
  ClearAllSelectedDungeons();
  return 0;
}

int LuaSetLFGComment(lua_State *L) {
  const char *raw_comment = lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr;
  auto &lfg = ::openwow::game::LFGSystem::Get();
  const std::string comment = NormalizeLfgComment(raw_comment);
  lfg.SetComment(comment);

  if (!lfg.HasActiveJoinRequest()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  if (!lfg.TryConsumeCommentSendThrottleToken()) {
    return 0;
  }

  session->interaction().SendLfgSetComment(comment);
  return 0;
}

int LuaCanPartyLFGBackfill(lua_State *L) {
  lua_pushboolean(L, ::openwow::game::GroupSystem::Get().CanPartyLfgBackfill());
  return 1;
}

int LuaIsPartyLFG(lua_State *L) {
  if (::openwow::game::GroupSystem::Get().HasPartyLfgDungeon()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsInLFGDungeon(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  const auto packed_dungeon_id = ::openwow::game::GroupSystem::Get().GetPartyLfgDungeonId();
  if (packed_dungeon_id == 0) {
    return 0;
  }

  const auto *dungeon = dbc->lfg_dungeons().LookupEntry(packed_dungeon_id & 0x00FFFFFFu);
  if (dungeon == nullptr) {
    return 0;
  }

  const bool is_current_lfg_dungeon =
      dungeon->map_id == ResolveCurrentMapId(*session) &&
      dungeon->difficulty == ResolveCurrentInstanceDifficultyIndex(*session);
  PushFrameScriptBoolean(L, is_current_lfg_dungeon);
  return 1;
}

int LuaPartyLFGStartBackfill(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  auto &group_system = ::openwow::game::GroupSystem::Get();
  if (!group_system.CanPartyLfgBackfill()) {
    return 0;
  }

  auto &lfg = ::openwow::game::LFGSystem::Get();
  lfg.SetSelectedDungeons({});
  lfg.SetSelectedDungeons({group_system.GetPartyLfgDungeonId()});
  if (!TrySendLfgJoin(L, *session, lfg.GetSelectedDungeons(), false)) {
    return 0;
  }

  lua_pushboolean(L, 1);
  return 1;
}

int LuaRefreshLFGList(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  session->lfg().RefreshPublishedSearchResults();
  FireLfgUpdateEvent(L);
  return 0;
}

int LuaRequestLFDPartyLockInfo(lua_State *L) {
  return HandleLfdLockInfoRequest(L, GetLfdPartyLockInfoRequestThrottleState(),
                                  openwow::net::wotlk::Opcode::CMSG_LFD_PARTY_LOCK_INFO_REQUEST);
}

int LuaRequestLFDPlayerLockInfo(lua_State *L) {
  return HandleLfdLockInfoRequest(L, GetLfdPlayerLockInfoRequestThrottleState(),
                                  openwow::net::wotlk::Opcode::CMSG_LFD_PLAYER_LOCK_INFO_REQUEST);
}

int LuaGetLFDChoiceCollapseState(lua_State *L) {
  PrepareOptionalOutputTable(
      L, "GetLFDChoiceCollapseState", "Usage: GetLFDChoiceCollapseState([table])");

  const auto *dbc = GetDbcLoaderLocal(L);
  const auto collapsed_headers = GetNormalizedPackedChoiceState("lfdCollapsedHeaders");
  const auto lfd_entries = BuildLfdChoiceEntries(dbc);
  const auto lfr_entries = BuildLfrChoiceEntries(dbc);

  const auto push_entry = [L, dbc, &collapsed_headers](const LfgChoiceEntry &entry) {
    std::uint32_t key = 0;
    if (entry.id < 0) {
      key = static_cast<std::uint32_t>(-entry.id);
      lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    } else {
      const auto *dungeon =
          dbc != nullptr ? dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(entry.id))
                         : nullptr;
      if (dungeon == nullptr) {
        return;
      }
      key = dungeon->group_id;
      lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    }

    lua_pushwowbool(L, PackedChoiceStateContains(collapsed_headers, key));
    lua_rawset(L, 1);
  };

  for (const auto &entry : lfd_entries) {
    push_entry(entry);
  }
  for (const auto &entry : lfr_entries) {
    push_entry(entry);
  }
  return 1;
}

int LuaGetLFDChoiceEnabledState(lua_State *L) {
  PrepareOptionalOutputTable(
      L, "GetLFDChoiceEnabledState", "Usage: GetLFDChoiceEnabledState([table])");

  const auto enabled_dungeons = GetNormalizedPackedChoiceState("lfdSelectedDungeons");
  const auto emit_enabled = [L, &enabled_dungeons](const std::vector<LfgChoiceEntry> &entries) {
    for (const auto &entry : entries) {
      if (entry.id <= 0) {
        continue;
      }
      const auto dungeon_id = static_cast<std::uint32_t>(entry.id);
      if (!PackedChoiceStateContains(enabled_dungeons, dungeon_id)) {
        continue;
      }

      lua_pushnumber(L, static_cast<lua_Number>(dungeon_id));
      FrameScript_PushBoolean(L, true);
      lua_rawset(L, 1);
    }
  };

  emit_enabled(BuildLfdChoiceEntries(GetDbcLoaderLocal(L)));
  emit_enabled(BuildLfrChoiceEntries(GetDbcLoaderLocal(L)));
  return 1;
}

int LuaGetLFDChoiceOrder(lua_State *L) {
  PrepareOptionalOutputTable(L, "GetLFDChoiceOrder", "Usage: GetLFDChoiceOrder([table])");

  const auto entries = BuildLfdChoiceEntries(GetDbcLoaderLocal(L));
  std::uint32_t index = 1;
  for (const auto &entry : entries) {
    lua_pushnumber(L, static_cast<lua_Number>(index++));
    lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    lua_rawset(L, 1);
  }
  return 1;
}

int LuaGetLFDChoiceInfo(lua_State *L) {
  PrepareOptionalOutputTable(L, "GetLFDChoiceInfo", "Usage: GetLFDChoiceInfo([table])");

  const auto *dbc = GetDbcLoaderLocal(L);
  const auto lfd_entries = BuildLfdChoiceEntries(dbc);
  const auto lfr_entries = BuildLfrChoiceEntries(dbc);

  for (const auto &entry : lfd_entries) {
    lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    lua_newtable(L);
    PushLfgChoiceInfoTable(L, dbc, entry.id);
    lua_rawset(L, 1);
  }

  for (const auto &entry : lfr_entries) {
    lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    lua_rawget(L, 1);
    if (lua_type(L, -1) > 0) {
      lua_pop(L, 1);
      continue;
    }

    lua_pop(L, 1);
    lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    lua_newtable(L);
    PushLfgChoiceInfoTable(L, dbc, entry.id);
    lua_rawset(L, 1);
  }
  return 1;
}

int LuaGetLFDChoiceLockedState(lua_State *L) {
  PrepareOptionalOutputTable(
      L, "GetLFDChoiceLockedState", "Usage: GetLFDChoiceLockedState([table])");

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 1;
  }

  for (const auto &lock : session->lfg().GetChoiceLockEntries()) {
    lua_pushnumber(L, static_cast<lua_Number>(lock.dungeon_entry & 0x00FFFFFFu));
    lua_pushnumber(L, static_cast<lua_Number>(lock.lock_status));
    lua_rawset(L, 1);
  }

  return 1;
}

int LuaGetLFDLockInfo(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: GetLFDLockInfo(dungeonID, playerIndex)");
  }

  const auto raw_dungeon_id = lua_tonumber(L, 1);
  if (raw_dungeon_id < 0.0) {
    return 0;
  }

  const auto player_index = static_cast<int>(lua_tonumber(L, 2));
  if (player_index < 1) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto dungeon_id = static_cast<std::uint32_t>(raw_dungeon_id);
  const auto &lfg = session->lfg();

  std::uint64_t player_guid = 0;
  std::uint32_t lock_reason = 0;

  if (player_index == 1) {
    if (!lfg.has_player_dungeon_info()) {
      return 0;
    }

    player_guid = session->objects().GetLocalPlayerGuid().GetRawValue();
    if (const auto reason = lfg.FindPlayerLockReason(dungeon_id); reason.has_value()) {
      lock_reason = *reason;
    }
  } else {
    const auto member_index = static_cast<std::size_t>(player_index - 2);
    const auto *member_locks = lfg.GetPartyLockInfo(member_index);
    if (member_locks == nullptr) {
      return 0;
    }

    player_guid = member_locks->guid;
    if (const auto reason = lfg.FindPartyLockReason(member_index, dungeon_id);
        reason.has_value()) {
      lock_reason = *reason;
    }
  }

  const auto snapshot = ResolveSearchSnapshot(session, player_guid);
  lua_pushstring(L, snapshot && !snapshot->name.empty() ? snapshot->name.c_str() : "UNKNOWN");
  lua_pushnumber(L, static_cast<lua_Number>(lock_reason));
  return 2;
}

int LuaGetLFDLockPlayerCount(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto count = session ? session->lfg().GetLfdLockPlayerCount() : 0;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaGetLFGCompletionRewardItem(lua_State *L) {
  const auto reward_number = ReadSignedI32Argument(
      L, 1, "Usage: GetLFGCompletionRewardItem(index)");
  if (reward_number <= 0) {
    return 0;
  }
  const auto reward_index = static_cast<std::size_t>(reward_number - 1);

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto *reward =
      session->lfg().FindCompletionRewardItemByIndex(reward_index);
  if (reward == nullptr) {
    return 0;
  }

  const auto texture_path = ResolveLfgRewardTexturePath(GetDbcLoaderLocal(L), reward->display_info_id);
  lua_pushstring(L, texture_path.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(reward->item_count));
  return 2;
}

int LuaGetLFGInfoLocal(lua_State *L) {
  const auto &lfg = ::openwow::game::LFGSystem::Get();
  lua_pushboolean(L, lfg.HasActiveJoinRequest());
  lua_pushboolean(L, 0);
  lua_pushboolean(L, 0);
  lua_pushstring(L, lfg.GetComment().c_str());
  lua_pushnumber(L, static_cast<lua_Number>(lfg.GetSelectedDungeons().size()));
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  return 8;
}

int LuaGetLFGInfoServer(lua_State *L) {
  bool using_party_update = false;
  const auto *update = GetActiveServerLfgInfo(GetWorldSession(L), &using_party_update);

  lua_pushwowbool(L, using_party_update);
  lua_pushwowbool(L, update != nullptr && update->has_extra);
  lua_pushwowbool(L, update != nullptr && update->queued);
  lua_pushwowbool(L, update != nullptr && update->raw_flag_4);
  lua_pushwowbool(L, update != nullptr && update->raw_flag_5);
  lua_pushstring(L, update != nullptr ? update->comment.c_str() : "");
  lua_pushnumber(L, static_cast<lua_Number>(update != nullptr ? update->dungeons.size() : 0u));

  const auto raw_tail_bytes =
      update != nullptr ? update->raw_tail_bytes : std::array<std::uint8_t, 3>{};
  for (const auto value : raw_tail_bytes) {
    lua_pushnumber(L, static_cast<lua_Number>(value));
  }
  return 10;
}

int LuaGetLFGProposalEncounter(lua_State *L) {
  const auto encounter_index =
      ReadOneBasedSaturatedIndexArgument(L, "Usage: GetLFGProposalEncounter(index)");
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || !session->lfg().proposal()) {
    return PushDungeonEncounter(L, dbc, nullptr, encounter_index, 0);
  }

  const auto &manager = session->lfg();
  return PushDungeonEncounter(L, dbc, LookupProposalDungeon(manager, dbc), encounter_index,
                              manager.proposal()->encounter_mask);
}

int LuaGetLFGProposalMember(lua_State *L) {
  const auto index_raw =
      ReadOneBasedSaturatedIndexArgument(L, "Usage: GetLFGProposalMember(index)");
  auto *session = GetWorldSession(L);
  if (session == nullptr || !session->lfg().proposal() || index_raw >= 5u) {
    return 0;
  }

  const auto &proposal = *session->lfg().proposal();
  const auto member_index = static_cast<std::size_t>(index_raw);
  const auto *member =
      member_index < proposal.players.size() ? &proposal.players[member_index] : nullptr;
  const auto role_flags = member != nullptr ? member->role : 0u;

  PushFrameScriptBoolean(L, (role_flags & kLfgLeaderRoleMask) != 0);
  lua_pushstring(L, LfgRoleToken(role_flags));
  lua_pushnumber(L, 0.0);
  PushFrameScriptBoolean(L, member != nullptr && member->has_answered);
  PushFrameScriptBoolean(L, member != nullptr && member->has_accepted);
  lua_pushnil(L);
  lua_pushnil(L);

  return 7;
}

int LuaGetLFDQueuedList(lua_State *L) {
  PrepareOptionalOutputTable(L, "GetLFDQueuedList", "Usage: GetLFDQueuedList([table])");

  const auto *update = GetActiveServerLfgInfo(GetWorldSession(L));
  if (update == nullptr) {
    return 1;
  }

  for (const auto dungeon_id : update->dungeons) {
    lua_pushnumber(L, static_cast<lua_Number>(dungeon_id & 0x00FFFFFFu));
    lua_pushwowbool(L, true);
    lua_settable(L, 1);
  }
  return 1;
}

int LuaGetLastQueueStatusIndex(lua_State *L) {
  const int index = FindLastQueueStatusIndexZeroBased(GetWorldSession(L));
  if (index < 0) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, static_cast<lua_Number>(index + 1));
  }
  return 1;
}

int LuaIsListedInLFR(lua_State *L) {
  const auto *update = GetActiveServerLfgInfo(GetWorldSession(L));
  const bool listed = ServerSnapshotHasSelectedDungeonType(update, GetDbcLoaderLocal(L), 2u);
  PushFrameScriptBoolean(L, listed);
  return 1;
}

int LuaGetLFGRandomCooldownExpiration(lua_State *L) {
  static constexpr std::uint32_t kLfgRandomCooldown = 71328;
  return PushLocalPlayerAuraRemainingSecondsIfPresent(
      L, GetWorldSession(L), kLfgRandomCooldown);
}

int LuaGetLFGRoleUpdate(lua_State *L) {
  const auto *role_check = GetRoleCheckUpdate(GetWorldSession(L));
  const auto active = role_check != nullptr && role_check->state == 2;
  const auto slot_count = role_check != nullptr ? role_check->dungeons.size() : 0u;
  const auto member_count = role_check != nullptr ? role_check->players.size() : 0u;

  lua_pushwowbool(L, active);
  lua_pushnumber(L, static_cast<lua_Number>(slot_count));
  lua_pushnumber(L, static_cast<lua_Number>(member_count));
  return 3;
}

int LuaGetLFGRoleUpdateMember(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *role_check = GetRoleCheckUpdate(session);
  const auto index =
      ReadOneBasedSaturatedIndexArgument(L, "Usage: GetLFGRoleUpdateMember(index)");
  if (role_check == nullptr) {
    return 0;
  }

  const auto *member =
      GetRoleCheckMemberByIndex(*role_check, static_cast<std::size_t>(index));
  if (member == nullptr) {
    return 0;
  }

  lua_pushwowbool(L, member->ready);
  lua_pushnumber(L, static_cast<lua_Number>(member->level));

  if (const auto snapshot =
          openwow::game::UnitQueryBridge::Get().GetPlayerInfoByGUID(session, member->guid);
      snapshot.has_value()) {
    const auto *class_token = ClassFileToken(snapshot->classId);
    lua_pushstring(L, snapshot->name.c_str());
    lua_pushstring(L, class_token != nullptr ? class_token : "");
  } else {
    PushNilValues(L, 2);
  }

  if (member->ready) {
    lua_pushwowbool(L, (member->roles & openwow::game::kLfgRoleLeader) != 0);
    lua_pushwowbool(L, (member->roles & openwow::game::kLfgRoleTank) != 0);
    lua_pushwowbool(L, (member->roles & openwow::game::kLfgRoleHealer) != 0);
    lua_pushwowbool(L, (member->roles & openwow::game::kLfgRoleDps) != 0);
  } else {
    PushNilValues(L, 4);
  }

  return 8;
}

int LuaGetLFGRoleUpdateSlot(lua_State *L) {
  const auto *role_check = GetRoleCheckUpdate(GetWorldSession(L));
  const auto index = ReadOneBasedSignedIndexArgument(L, "Usage: GetLFGRoleUpdateSlot(index)");
  if (role_check == nullptr || index < 0 ||
      static_cast<std::size_t>(index) >= role_check->dungeons.size()) {
    return 0;
  }

  const auto packed_slot = role_check->dungeons[static_cast<std::size_t>(index)];
  lua_pushnumber(L, static_cast<lua_Number>((packed_slot >> 24) & 0xFFu));
  lua_pushnumber(L, static_cast<lua_Number>(packed_slot & 0x00FFFFFFu));
  return 2;
}

int LuaSearchLFGGetNumResults(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || !session->lfg().has_search_results()) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
  }

  lua_pushnumber(L, static_cast<lua_Number>(session->lfg().search_result_count()));
  lua_pushnumber(L, static_cast<lua_Number>(session->lfg().search_result_total_count()));
  return 2;
}

int LuaClearLFGDungeon(lua_State *L) {
  constexpr auto kUsage = "Usage: ClearLFGDungeon(type, id)";

  if (!lua_isnumber(L, 1) && !lua_isnumber(L, 2)) {
    return luaL_error(L, kUsage);
  }
  const auto type_id = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  const auto dungeon_id = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 2));
  const auto *dbc = GetDbcLoaderLocal(L);

  const auto *dungeon =
      dbc != nullptr ? dbc->lfg_dungeons().LookupEntry(dungeon_id) : nullptr;
  if (type_id >= 7u || dungeon == nullptr) {
    return 0;
  }

  ::openwow::game::LFGSystem::Get().RemoveSelectedDungeon(
      PackDungeonSelectionId(type_id, dungeon_id));
  return 0;
}

int LuaIsLFGDungeonJoinable(lua_State *L) {
  const auto dungeon_id =
      ReadSignedI32Argument(L, 1, "Usage: IsLFGDungeonJoinable(dungeonID)");
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr || dungeon_id < 0) {
    return 0;
  }

  const auto *dungeon =
      dbc->lfg_dungeons().LookupEntry(static_cast<std::uint32_t>(dungeon_id));
  if (dungeon == nullptr) {
    return 0;
  }

  lua_pushwowbool(L, session->lfg().IsDungeonJoinable(PackDungeonId(*dungeon)));
  return 1;
}

int LuaSearchLFGGetEncounterResults(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  const auto result_index = static_cast<std::size_t>(luaL_checknumber(L, 1));
  const auto encounter_index = static_cast<std::uint32_t>(luaL_checknumber(L, 2));
  if (session == nullptr || !session->lfg().has_search_results())
    return 0;

  std::vector<SearchDisplayEntry> display;
  const auto *entry = FindSearchDisplayEntry(session, session->lfg(), dbc, result_index, display);
  if (entry == nullptr || entry->player == nullptr)
    return 0;

  const auto completion_mask =
      entry->group != nullptr ? entry->group->encounter_mask : entry->player->secondary_mask;
  return PushSearchEncounter(L, session->lfg(), dbc, encounter_index - 1, completion_mask);
}

int LuaSearchLFGGetPartyResults(lua_State *L) {
  constexpr auto kUsage = "Usage: SearchLFGGetPartyResults(index, partyIndex)";
  const auto result_index = ReadSaturatedU32Argument(L, 1, kUsage);
  const auto party_index = ReadSaturatedU32Argument(L, 2, kUsage);
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || !session->lfg().has_search_results())
    return 0;

  std::vector<SearchDisplayEntry> display;
  const auto *entry = FindSearchDisplayEntry(session, session->lfg(), dbc, result_index, display);
  if (entry == nullptr || entry->group == nullptr)
    return 0;

  const auto *member =
      session->lfg().GetSearchGroupMember(entry->group->guid, party_index);
  if (member == nullptr)
    return PushUnavailableSearchPartyResult(L);

  const auto snapshot = ResolveSearchSnapshot(session, member->guid);
  if (!snapshot.has_value())
    return PushUnavailableSearchPartyResult(L);

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kSearchPartyResultCount, "LFG search party values");
  const std::string filtered_comment = FilterSearchComment(member->comment);

  lua_pushstring(L, snapshot->name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(member->level));

  if (session->social().IsIgnored(openwow::game::ObjectGuid(member->guid))) {
    lua_pushstring(L, "ignored");
  } else if (session->social().IsFriend(openwow::game::ObjectGuid(member->guid))) {
    lua_pushstring(L, "friend");
  } else {
    lua_pushnil(L);
  }

  PushSearchClassDisplay(L, snapshot, true);
  lua_pushstring(L, LookupAreaName(dbc, member->area_id).c_str());
  lua_pushstring(L, filtered_comment.c_str());
  (void)PushSearchExtendedFields(L, member);
  return result_count;
}

int LuaSearchLFGGetResults(lua_State *L) {
  const auto result_index =
      ReadSignedI32Argument(L, 1, "Usage: SearchLFGGetResults(index)");
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || !session->lfg().has_search_results() || result_index <= 0)
    return 0;

  std::vector<SearchDisplayEntry> display;
  const auto *entry = FindSearchDisplayEntry(session, session->lfg(), dbc, result_index, display);
  if (entry == nullptr || entry->player == nullptr)
    return 0;

  const auto snapshot = ResolveSearchSnapshot(session, entry->player->guid);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kSearchPlayerResultCount, "LFG search player values");
  if (snapshot.has_value()) {
    lua_pushstring(L, snapshot->name.c_str());
  } else {
    PushLocalizedUnknown(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(entry->player->level));
  lua_pushstring(L, LookupAreaName(dbc, entry->player->area_id).c_str());
  PushSearchClassDisplay(L, snapshot, false);
  const std::string filtered_comment = FilterSearchComment(entry->group != nullptr
                                                               ? entry->group->comment
                                                               : entry->player->comment);
  lua_pushstring(L, filtered_comment.c_str());

  const auto extra_party_members = entry->group != nullptr && entry->group->member_guids.size() > 1
                                       ? entry->group->member_guids.size() - 1
                                       : 0;
  lua_pushnumber(L, static_cast<lua_Number>(extra_party_members));
  lua_pushwowbool(L, entry->player->role_byte != 0);

  if (snapshot) {
    if (const auto *class_file = ClassFileToken(snapshot->classId)) {
      lua_pushstring(L, class_file);
    } else {
      lua_pushnil(L);
    }
  } else {
    lua_pushnil(L);
  }

  const auto completion_mask =
      entry->group != nullptr ? entry->group->encounter_mask : entry->player->secondary_mask;
  const auto [total_encounters, completed_encounters] =
      CountDungeonEncounters(session->lfg(), dbc, completion_mask);
  lua_pushnumber(L, static_cast<lua_Number>(total_encounters));
  lua_pushnumber(L, static_cast<lua_Number>(completed_encounters));
  (void)PushSearchExtendedFields(L, entry->player);
  return result_count;
}

int LuaSearchLFGGetJoinedID(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto joined_search_id = session->lfg().joined_search_id() & 0x00FFFFFFu;
  if (joined_search_id == 0) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(joined_search_id));
  return 1;
}

int LuaSearchLFGJoin(lua_State *L) {
  constexpr auto kUsage = "Usage: SearchLFGJoin(typeID, lfgID)";
  const auto type_id = ReadSaturatedU32Argument(L, 1, kUsage);
  const auto dungeon_id = ReadSaturatedU32Argument(L, 2, kUsage);

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (session == nullptr || dbc == nullptr || !GameUI_CanPerformProtectedAction(protected_action_kind::kLookingForGroup)) {
    return 0;
  }

  if (type_id >= 7) {
    return 0;
  }

  if (dbc->lfg_dungeons().LookupEntry(dungeon_id) == nullptr) {
    return 0;
  }

  const auto packed_search_id = (type_id << 24) | (dungeon_id & 0x00FFFFFFu);
  if (session->lfg().joined_search_id() == packed_search_id) {
    return 0;
  }

  session->interaction().SendLfgSearchJoin(packed_search_id);
  FireLfgUpdateEvent(L);
  return 0;
}

int LuaSearchLFGLeave(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session != nullptr && GameUI_CanPerformProtectedAction(protected_action_kind::kLookingForGroup)) {
    if (session->lfg().joined_search_id() == 0) {
      return 0;
    }

    session->interaction().SendLfgSearchLeave();
    FireLfgUpdateEvent(L);
  }
  return 0;
}

int LuaSearchLFGSort(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr)
    return 0;
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: SearchLFGSort(\"type\")");
  }

  const auto sort_key = SafeLuaString(L, 1);
  if (EqualsAsciiNoCase(sort_key, "group")) {
    session->lfg().ToggleSearchGroupOrdering();
    FireLfgUpdateEvent(L);
    return 0;
  }

  auto key = ::openwow::game::LfgSearchSortKey::kName;
  if (EqualsAsciiNoCase(sort_key, "level")) {
    key = ::openwow::game::LfgSearchSortKey::kLevel;
  } else if (EqualsAsciiNoCase(sort_key, "class")) {
    key = ::openwow::game::LfgSearchSortKey::kClass;
  } else if (EqualsAsciiNoCase(sort_key, "zone")) {
    key = ::openwow::game::LfgSearchSortKey::kZone;
  } else if (EqualsAsciiNoCase(sort_key, "tank")) {
    key = ::openwow::game::LfgSearchSortKey::kTank;
  } else if (EqualsAsciiNoCase(sort_key, "healer")) {
    key = ::openwow::game::LfgSearchSortKey::kHealer;
  } else if (EqualsAsciiNoCase(sort_key, "damage")) {
    key = ::openwow::game::LfgSearchSortKey::kDamage;
  }

  session->lfg().PromoteSearchSortKey(key);
  session->lfg().ResortSearchResults(session, GetDbcLoaderLocal(L));
  FireLfgUpdateEvent(L);
  return 0;
}

int LuaGetLFRChoiceOrder(lua_State *L) {
  PrepareOptionalOutputTable(L, "GetLFRChoiceOrder", "Usage: GetLFRChoiceOrder([table])");

  std::uint32_t index = 1;
  for (const auto &entry : BuildLfrChoiceEntries(GetDbcLoaderLocal(L))) {
    lua_pushnumber(L, static_cast<lua_Number>(index++));
    lua_pushnumber(L, static_cast<lua_Number>(entry.id));
    lua_rawset(L, 1);
  }
  return 1;
}

int LuaSetLFGDungeonEnabled(lua_State *L) {
  const auto dungeon_id = ReadSignedI32Argument(
      L, 1, "Usage: SetLFGDungeonEnabled(dungeonID, isEnabled)");
  const auto enabled = ScriptReadBoolArgOrDefault(L, 2, true);
  if (dungeon_id > 0) {

    SetArchivedDungeonChoiceEnabled(static_cast<std::uint32_t>(dungeon_id), enabled);
  }
  return 0;
}

int LuaSetLFGHeaderCollapsed(lua_State *L) {
  const auto header_id = ReadSignedI32Argument(
      L, 1, "Usage: SetLFGHeaderCollapsed(headerID, isCollapsed)");
  const auto collapsed = ScriptReadBoolArgOrDefault(L, 2, false);
  const auto *dbc = GetDbcLoaderLocal(L);
  if (header_id != std::numeric_limits<std::int32_t>::min() && header_id < 0 &&
      dbc != nullptr &&
      dbc->lfg_dungeon_group().LookupEntry(static_cast<std::uint32_t>(-header_id)) != nullptr) {
    UpdatePackedChoiceState("lfdCollapsedHeaders", static_cast<std::uint32_t>(-header_id),
                            collapsed);
  }
  return 0;
}

int LuaUnitHasLFGDeserter(lua_State *L) {
  return LuaUnitHasTrackedAura(L, "Usage: UnitHasLFGDeserter(\"unit\")", 71041);
}

int LuaUnitHasLFGRandomCooldown(lua_State *L) {
  return LuaUnitHasTrackedAura(L, "Usage: UnitHasLFGRandomCooldown(\"unit\")", 71328);
}

int LuaGetLFGTypes(lua_State *L) {
  static constexpr std::array<std::string_view, 7> kTypeLabels = {
      "LFG_TYPE_NONE",           "LFG_TYPE_DUNGEON", "LFG_TYPE_RAID",
      "LFG_TYPE_QUEST",          "LFG_TYPE_ZONE",    "LFG_TYPE_HEROIC_DUNGEON",
      "LFG_TYPE_RANDOM_DUNGEON",
  };

  const auto &localization = openwow::game::Localization::Get();
  for (const auto key : kTypeLabels) {
    const auto label = localization.GetString(std::string(key), "");
    lua_pushlstring(L, label.data(), label.size());
  }
  return static_cast<int>(kTypeLabels.size());
}

}

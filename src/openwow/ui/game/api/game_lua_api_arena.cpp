
#include "openwow/ui/game/api/game_lua_api_arena.h"
#include "openwow/core/storm_string.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/petition_frame.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kArenaTeamSlotCount = 3;
constexpr std::size_t kArenaTeamNameLimit = 0x30;
constexpr std::size_t kArenaRosterInfoResultCount = 10;

bool HasActiveArenaPlayer(const openwow::game::WorldSession& session) {
  return session.objects().GetLocalPlayerTyped() != nullptr;
}

void PushEmptyArenaRosterInfo(lua_State* L) {
  lua_pushnil(L);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
}

std::uint32_t ReadRequiredArenaTeamIndex(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "%s", usage);
  }
  return openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
}

std::uint32_t ReadRequiredArenaRosterIndex(lua_State* L, const int argument,
                                           const char* usage) {
  if (!lua_isnumber(L, argument)) {
    luaL_error(L, "%s", usage);
  }
  return openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, argument));
}

std::uint32_t ResolveArenaTeamIdFromIndex(const std::uint32_t team_index) {
  const auto slot = team_index - 1u;
  if (slot >= kArenaTeamSlotCount) {
    return 0;
  }

  const auto *team = ::openwow::game::ArenaSystem::Get().GetTeam(
      static_cast<std::uint8_t>(slot));
  return team != nullptr ? team->team_id : 0u;
}

std::string ReadArenaTeamNameArgument(lua_State *L, const char *usage) {
  if (!lua_isstring(L, 2)) {
    luaL_error(L, "%s", usage);
  }

  const char *name = lua_tostring(L, 2);
  if (name != nullptr && std::char_traits<char>::length(name) > kArenaTeamNameLimit) {
    luaL_error(L, "Name too long");
  }

  return name != nullptr ? std::string(name) : std::string{};
}

void FireArenaRosterUpdateForLoadedTeams(const openwow::game::WorldSession& session) {
  for (std::uint8_t slot = 0; slot < kArenaTeamSlotCount; ++slot) {
    if (!session.battleground().GetArenaRoster(slot).members.empty()) {
      ScriptEventDispatch::Get().FireEvent(events::ARENA_TEAM_ROSTER_UPDATE);
    }
  }
}

[[nodiscard]] std::string_view ResolveArenaRosterClassDisplayName(
    const openwow::game::WorldSession& session,
    const openwow::game::ArenaTeamMember& member) {
  const auto* dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return {};
  }

  const auto* class_entry = dbc->chr_classes().LookupEntry(member.class_id);
  if (class_entry == nullptr) {
    return {};
  }

  if (const auto* name_info = session.query_cache().GetPlayerName(member.guid);
      name_info != nullptr) {
    const std::string_view display_name = class_entry->DisplayNameForSex(name_info->sex);
    if (!display_name.empty()) {
      return display_name;
    }
  }

  return class_entry->name;
}

}

int LuaGetArenaTeamRosterInfo(lua_State *L) {
  const auto team_idx = ReadRequiredArenaRosterIndex(
      L, 1, "Usage: GetArenaTeamRosterInfo(team, index)");
  const auto index = ReadRequiredArenaRosterIndex(
      L, 2, "Usage: GetArenaTeamRosterInfo(team, index)");
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, kArenaRosterInfoResultCount, "arena roster values");

  auto* session = GetWorldSession(L);
  if (session == nullptr || team_idx < 1 || team_idx > kArenaTeamSlotCount ||
      index < 1) {
    PushEmptyArenaRosterInfo(L);
    return result_count;
  }

  const auto* member = session->battleground().GetArenaRosterMember(
      static_cast<std::uint8_t>(team_idx - 1), static_cast<std::size_t>(index - 1));
  if (member == nullptr) {
    PushEmptyArenaRosterInfo(L);
    return result_count;
  }

  lua_pushstring(L, member->name.c_str());
  lua_pushnumber(L, static_cast<lua_Number>(member->rank));
  lua_pushnumber(L, static_cast<lua_Number>(member->level));
  if (const std::string_view class_name =
          ResolveArenaRosterClassDisplayName(*session, *member);
      !class_name.empty()) {
    lua_pushlstring(L, class_name.data(), class_name.size());
  } else {
    lua_pushnil(L);
  }
  if (member->online) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(member->week_games));
  lua_pushnumber(L, static_cast<lua_Number>(member->week_wins));
  lua_pushnumber(L, static_cast<lua_Number>(member->season_games));
  lua_pushnumber(L, static_cast<lua_Number>(member->season_wins));
  lua_pushnumber(L, static_cast<lua_Number>(member->personal_rating));
  return result_count;
}

int LuaGetNumArenaTeamMembers(lua_State *L) {
  const auto team_idx = ReadRequiredArenaRosterIndex(
      L, 1, "Usage: GetNumArenaTeamMembers(team [,showOffline])");
  const bool show_offline = ScriptReadBoolArgOrDefault(L, 2, false);

  auto* session = GetWorldSession(L);
  const auto count =
      session != nullptr && team_idx >= 1 && team_idx <= kArenaTeamSlotCount
          ? session->battleground().GetArenaRosterMemberCount(
                static_cast<std::uint8_t>(team_idx - 1), show_offline)
          : 0;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaAcceptArenaTeam(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr || !HasActiveArenaPlayer(*session)) {
    return 0;
  }

  session->interaction().SendArenaTeamAccept();
  return 0;
}

int LuaDeclineArenaTeam(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr || !HasActiveArenaPlayer(*session)) {
    return 0;
  }

  session->interaction().SendArenaTeamDecline();
  return 0;
}

int LuaArenaTeamLeave(lua_State *L) {
  const auto team_idx = ReadRequiredArenaTeamIndex(L, "Usage: ArenaTeamLeave(team)");
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendArenaTeamLeave(ResolveArenaTeamIdFromIndex(team_idx));
  return 0;
}

int LuaArenaTeamDisband(lua_State *L) {
  const auto team_idx = ReadRequiredArenaTeamIndex(L, "Usage: ArenaTeamDisband(team)");
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendArenaTeamDisband(ResolveArenaTeamIdFromIndex(team_idx));
  return 0;
}

int LuaArenaTeamInviteByName(lua_State *L) {
  const auto team_idx =
      ReadRequiredArenaTeamIndex(L, "Usage: ArenaTeamInviteByName(team, name)");
  const std::string name = ReadArenaTeamNameArgument(L, "Usage: ArenaTeamInviteByName(team, name)");
  auto *session = GetWorldSession(L);
  if (!session || name.empty())
    return 0;
  session->interaction().SendArenaTeamInvite(ResolveArenaTeamIdFromIndex(team_idx), name);
  return 0;
}

int LuaArenaTeamUninviteByName(lua_State *L) {
  const auto team_idx = ReadRequiredArenaTeamIndex(
      L, "Usage: ArenaTeamUninviteByName(team, name)");
  const std::string name =
      ReadArenaTeamNameArgument(L, "Usage: ArenaTeamUninviteByName(team, name)");
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendArenaTeamRemove(ResolveArenaTeamIdFromIndex(team_idx), name);
  return 0;
}

int LuaArenaTeamSetLeaderByName(lua_State *L) {
  const auto team_idx = ReadRequiredArenaTeamIndex(
      L, "Usage: ArenaTeamSetLeaderByName(team, name)");
  const std::string name =
      ReadArenaTeamNameArgument(L, "Usage: ArenaTeamSetLeaderByName(team, name)");
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  session->interaction().SendArenaTeamLeader(ResolveArenaTeamIdFromIndex(team_idx), name);
  return 0;
}

int LuaIsArenaTeamCaptain(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsArenaTeamCaptain(index)");
  }

  auto* session = GetWorldSession(L);
  const auto* player = session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto slot_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (player == nullptr || slot_index >= 3) {
    lua_pushnil(L);
    return 1;
  }

  if (player->GetArenaTeamInfo(static_cast<std::uint8_t>(slot_index)).IsLocalPlayerCaptain()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetNumArenaOpponents(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto count =
      session != nullptr ? session->battleground().GetArenaOpponentSlotCount() : 0u;
  lua_pushnumber(L, static_cast<lua_Number>(count));
  return 1;
}

int LuaRequestBattlefieldPositions(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto &battlefield_info = ::openwow::game::BattlefieldInfo::Get();
  if (!battlefield_info.HasActiveBattlefieldInstance()) {
    battlefield_info.ClearPlayerPositions();
    return 0;
  }

  const auto now_tick = ::openwow::core::GameClock::GetTickCount32();
  if (!battlefield_info.CanRequestPlayerPositions(now_tick)) {
    return 0;
  }

  net::wotlk::WorldPacket pkt(net::wotlk::Opcode::MSG_BATTLEGROUND_PLAYER_POSITIONS);
  session->interaction().SendRawPacket(pkt);
  battlefield_info.MarkPlayerPositionsRequested(now_tick);
  return 0;
}

int LuaGetNumBattlefieldPositions(lua_State *L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(
                        ::openwow::game::BattlefieldInfo::Get().GetVisiblePlayerPositionCount(
                            session->objects())));
  return 1;
}

int LuaGetBattlefieldPosition(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetBattlefieldPosition(index)");
  }

  auto &battlefield_info = ::openwow::game::BattlefieldInfo::Get();
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 3;
  }
  const auto position_index = openwow::ui::ClampLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto visible_position =
      battlefield_info.GetVisiblePlayerMapPosition(session->objects(), position_index);
  if (!visible_position) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnil(L);
    return 3;
  }

  lua_pushnumber(L, static_cast<lua_Number>(visible_position->x));
  lua_pushnumber(L, static_cast<lua_Number>(visible_position->y));

  const auto request_name_query = [session](const std::uint64_t raw_guid) {
    if (session == nullptr || raw_guid == 0) {
      return;
    }

    (void)session->query_cache().RequestNameQuery(raw_guid);
  };

  const auto *name_info =
      session != nullptr ? battlefield_info.GetBattlefieldPositionName(
                               visible_position->guid.GetRawValue(), session->query_cache(),
                               request_name_query)
                         : nullptr;
  if (name_info == nullptr) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, name_info->name.c_str());
  }
  return 3;
}

int LuaSetBattlefieldScoreFaction(lua_State *L) {
  int faction = -1;
  if (lua_isnumber(L, 1)) {
    faction = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
    if (faction != -1 && faction != 0 && faction != 1) {
      return 0;
    }
  }

  auto &battlefield_info = openwow::game::BattlefieldInfo::Get();
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  battlefield_info.SetFactionFilter(faction);
  battlefield_info.UpdateScoreUI(session->objects());
  return 0;
}

int LuaArenaTeamRoster(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: ArenaTeamRoster(team)");
  }
  const auto team_idx =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (team_idx < 1 || team_idx > 3)
    return 0;

  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  (void)session->RequestActivePlayerArenaRoster(static_cast<std::uint8_t>(team_idx - 1));
  return 0;
}

int LuaBuyPetition(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: BuyPetition(index, name)");
  }

  const auto selection_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  const auto petition_name = SafeLuaString(L, 2);
  if (::openwow::game::PetitionFrame_ValidateRename(petition_name.c_str()) != 0) {
    lua_pushnil(L);
    return 1;
  }

  if (auto *session = GetWorldSession(L)) {
    ::openwow::game::PetitionFrame_BuyPetition(*session, selection_index,
                                               petition_name.c_str());
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaSortArenaTeamRoster(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usgae: SortArenaTeamRoster(\"type\")");
  }

  const char *sort_type = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (session == nullptr || sort_type == nullptr) {
    return 0;
  }

  auto field = ::openwow::game::ArenaRosterSortField::kName;
  if (openwow::core::SStrCmpNoCase(sort_type, "name", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kName;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "class", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kClass;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "played", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kPlayed;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "won", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kWins;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "seasonplayed", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kSeasonPlayed;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "seasonwon", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kSeasonWon;
  } else if (openwow::core::SStrCmpNoCase(sort_type, "rating", 0x7FFFFFFF) == 0) {
    field = ::openwow::game::ArenaRosterSortField::kRating;
  }

  session->battleground().SortArenaRosters(field);
  FireArenaRosterUpdateForLoadedTeams(*session);
  return 0;
}

int LuaTurnInArenaPetition(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: TurnInArenaPetition(teamSize)");
  }

  const auto team_size = openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (session->petition().petition_vendor_guid() == 0) {
    return 0;
  }

  std::array<std::uint32_t, 5> extra_fields{
      0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

  bool has_complete_design = true;
  for (int argument = 2; argument <= 12; ++argument) {
    if (!lua_isnumber(L, argument)) {
      has_complete_design = false;
      break;
    }
  }

  if (has_complete_design) {
    const auto pack_rgb = [L](const int first_argument) {
      std::uint32_t packed = 0xFF000000u;
      for (int component = 0; component < 3; ++component) {
        const auto value =
            static_cast<float>(lua_tonumber(L, first_argument + component));
        float scaled = 0.0f;
        if (value >= 0.0f) {
          scaled = value >= 1.0f ? 255.0f : value * 255.0f;
        }
        scaled += 0.5f;
        if (scaled <= 0.0f) {
          scaled = 0.0f;
        }
        const auto byte = static_cast<std::uint32_t>(scaled) & 0xFFu;
        packed |= byte << (16 - component * 8);
      }
      return packed;
    };

    extra_fields = {
        pack_rgb(2),
        static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 5))),
        static_cast<std::uint32_t>(TruncateLuaNumberToSseI32(lua_tonumber(L, 9))),
        pack_rgb(6),
        pack_rgb(10),
    };
  }

  ::openwow::game::PetitionFrame_TurnInArenaPetition(
      *session, team_size, extra_fields);
  return 0;
}

int LuaGetCurrentArenaSeason(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const std::int32_t value = session
      ? session->world_states().GetWorldState(WorldStateId::kCurrentArenaSeason)
      : 0;
  lua_pushnumber(L, static_cast<lua_Number>(value));
  return 1;
}

int LuaGetPreviousArenaSeason(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const std::int32_t value = session
      ? session->world_states().GetWorldState(WorldStateId::kPreviousArenaSeason)
      : 0;
  lua_pushnumber(L, static_cast<lua_Number>(value));
  return 1;
}

int LuaCloseArenaTeamRoster(lua_State* ) {

  return 0;
}

}

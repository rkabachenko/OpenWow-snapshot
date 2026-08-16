
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/api/game_lua_api_instance.h"
#include "openwow/ui/game/api/game_lua_api_raid.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/group_manager.h"
#include "openwow/game/group_system.h"

#include <cmath>
#include <limits>

namespace openwow::ui::game::detail {

namespace {

constexpr int kPartyAssignmentMissingGroupMessage = 80;
constexpr int kPartyAssignmentPermissionMessage = 84;
constexpr int kPartyAssignmentUnknownTargetMessage = 199;
constexpr int kPartyAssignmentUnitNotInGroupMessage = 314;
constexpr int kPartyAssignmentNonPlayerTargetMessage = 466;
constexpr int kPartyAssignmentRoleMainTank = 0;
constexpr int kPartyAssignmentRoleMainAssist = 1;

std::optional<std::uint8_t> ParsePartyAssignmentRole(const char* assignment) {
  if (assignment == nullptr) {
    return std::nullopt;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(assignment, "MAINTANK")) {
    return kPartyAssignmentRoleMainTank;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(assignment, "MAINASSIST")) {
    return kPartyAssignmentRoleMainAssist;
  }
  return std::nullopt;
}

std::uint8_t ResolvePartyAssignmentRoleMask(const std::uint8_t role) {
  switch (role) {
    case kPartyAssignmentRoleMainTank:
      return static_cast<std::uint8_t>(
          ::openwow::game::GroupMemberFlag::kMainTank);
    case kPartyAssignmentRoleMainAssist:
      return static_cast<std::uint8_t>(
          ::openwow::game::GroupMemberFlag::kMainAssist);
    default:
      return 0;
  }
}

int PushPartyAssignmentIndices(lua_State* L,
                               const ::openwow::game::GroupSystem& group_system,
                               const bool use_raid_roster,
                               const std::uint8_t assignment_mask) {
  int result_count = 0;

  if (use_raid_roster) {
    const auto member_count = group_system.GetNumGroupMembers();
    for (std::size_t roster_index = 0; roster_index < member_count;
         ++roster_index) {
      const auto* member = group_system.GetMember(roster_index);
      if (member == nullptr || (member->flags & assignment_mask) == 0) {
        continue;
      }

      lua_pushnumber(L, static_cast<lua_Number>(roster_index + 1));
      ++result_count;
    }
    return result_count;
  }

  for (std::uint32_t slot = 0; slot < 4; ++slot) {
    const auto member_guid = group_system.GetTrackedPartyMemberGuid(slot);
    if (member_guid == 0) {
      continue;
    }

    if ((group_system.GetMemberFlags(member_guid) & assignment_mask) == 0) {
      continue;
    }

    lua_pushnumber(L, static_cast<lua_Number>(slot + 1));
    ++result_count;
  }

  return result_count;
}

bool HasPartyAssignmentAuthority(const openwow::game::WorldSession& session) {
  auto& group_system = ::openwow::game::GroupSystem::Get();
  if (group_system.GetTrackedPartyMemberCount() == 0 &&
      group_system.GetNumGroupMembers() == 0) {
    DisplaySystemMessage(kPartyAssignmentMissingGroupMessage);
    return false;
  }

  const auto active_player_guid =
      session.objects().GetLocalPlayerGuid().GetRawValue();
  if (group_system.GetLeaderGuid() == active_player_guid) {
    return true;
  }

  if (active_player_guid != 0 &&
      group_system.GetMemberFlags(active_player_guid) != 0) {
    return true;
  }

  DisplaySystemMessage(kPartyAssignmentPermissionMessage);
  return false;
}

bool IsPartyAssignmentThrottleReady() {
  const auto now_tick = openwow::core::GameClock::GetTickCount32();
  return ::openwow::game::GroupSystem::Get().CanSendPartyAssignmentChange(
      now_tick);
}

void MarkPartyAssignmentSent() {
  ::openwow::game::GroupSystem::Get().MarkPartyAssignmentChangeSent(
      openwow::core::GameClock::GetTickCount32());
}

int PushRealLeaderMatch(lua_State* L, const bool require_non_zero_leader_guid) {
  const std::uint64_t leader_guid =
      ::openwow::game::GroupSystem::Get().GetRealLeaderGuid();
  const auto* session = GetWorldSession(L);
  const std::uint64_t active_player_guid =
      session != nullptr
          ? session->objects().GetActivePlayerGuid().GetRawValue()
          : 0;

  if ((!require_non_zero_leader_guid || leader_guid != 0) &&
      active_player_guid == leader_guid) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

bool TryResolvePartyAssignmentTarget(openwow::game::WorldSession& session,
                                     const char* token_or_name,
                                     bool exact_match,
                                     std::uint64_t* out_guid) {
  if (!out_guid) {
    return false;
  }
  *out_guid = 0;

  const auto party_member_guid = ResolveGameUiLookup(
      &session, token_or_name, ::openwow::game::kTypeMaskPlayer, 6,
      exact_match, false);
  if (!party_member_guid.IsEmpty()) {
    *out_guid = party_member_guid.GetRawValue();
    return true;
  }

  const auto any_unit_guid = ResolveGameUiLookup(
      &session, token_or_name, ::openwow::game::kTypeMaskUnit, 0,
      exact_match, false);
  if (!any_unit_guid.IsEmpty()) {
    DisplaySystemMessage(kPartyAssignmentNonPlayerTargetMessage);
    return false;
  }

  if (token_or_name != nullptr && token_or_name[0] != '\0' &&
      !openwow::text::EqualsIgnoreCaseAscii(token_or_name, "target")) {
    DisplaySystemMessage(kPartyAssignmentUnitNotInGroupMessage);
    return false;
  }

  DisplaySystemMessage(kPartyAssignmentUnknownTargetMessage);
  return false;
}

std::optional<std::size_t> ParseRaidRosterSelectionIndex(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    luaL_error(L, "Usage: SetRaidRosterSelection(index)");
    return std::nullopt;
  }

  const lua_Number raw_index = lua_tonumber(L, 1);
  if (!std::isfinite(raw_index)) {
    return std::nullopt;
  }

  const lua_Number truncated_index = std::trunc(raw_index);
  constexpr lua_Number kMinInt32 =
      static_cast<lua_Number>(std::numeric_limits<std::int32_t>::min());
  constexpr lua_Number kMaxInt32 =
      static_cast<lua_Number>(std::numeric_limits<std::int32_t>::max());
  if (truncated_index < kMinInt32 || truncated_index > kMaxInt32) {
    return std::nullopt;
  }

  const auto one_based_index = static_cast<std::int32_t>(truncated_index);
  if (one_based_index <= 0) {
    return std::nullopt;
  }

  return static_cast<std::size_t>(one_based_index - 1);
}

int LuaChangePartyAssignment(lua_State* L, const bool apply) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (!HasPartyAssignmentAuthority(*session)) {
    return 0;
  }

  if (!lua_isstring(L, 1)) {
    return luaL_error(
        L,
        "Usage: %s(\"assignment\" [,\"raidmember\"] [,exactMatch])",
        apply ? "SetPartyAssignment" : "ClearPartyAssignment");
  }

  const auto role = ParsePartyAssignmentRole(lua_tostring(L, 1));
  if (!role.has_value()) {
    return luaL_error(L, "Invalid Party assignment");
  }

  if (!IsPartyAssignmentThrottleReady()) {
    return 0;
  }

  std::uint64_t target_guid = 0;
  if (lua_isstring(L, 2)) {
    const char* token_or_name = lua_tostring(L, 2);
    const bool exact_match = ReadClientBoolArgOrDefault(L, 3, false);
    if (!TryResolvePartyAssignmentTarget(
            *session, token_or_name, exact_match, &target_guid)) {
      return 0;
    }
  }

  if (session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  MarkPartyAssignmentSent();
  session->interaction().SendPartyAssignment(*role, apply, target_guid);
  return 0;
}

}

int LuaGetRaidTargetIndex(lua_State* L) {
  auto* session = GetWorldSession(L);
  auto uid = std::string(luaL_optstring(L, 1, "target"));
  if (!session) { lua_pushnil(L); return 1; }

  auto guid = ResolveUnitId(session, uid);
  if (guid.IsEmpty()) { lua_pushnil(L); return 1; }

  auto& gs = ::openwow::game::GroupSystem::Get();
  uint8_t idx = gs.GetRaidTargetIndex(guid.GetRawValue());
  if (idx == 0xFF) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, static_cast<lua_Number>(idx + 1));
  }
  return 1;
}

int LuaGetRaidRosterSelection(lua_State* L) {
  const auto selected_index =
      ::openwow::game::GroupSystem::Get().FindRaidRosterSelection();
  lua_pushnumber(L, static_cast<lua_Number>(selected_index + 1));
  return 1;
}

int LuaSetRaidTarget(lua_State* L) {
  if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetRaidTarget(unit, index)");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto guid = ResolveUnitId(session, lua_tostring(L, 1));
  if (guid.IsEmpty()) {
    return 0;
  }

  auto& group_system = ::openwow::game::GroupSystem::Get();
  std::uint64_t target_guid = guid.GetRawValue();
  std::uint32_t icon =
      (TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 2)) & 0xFFu) - 1u;

  if (icon >= 8) {
    const std::uint8_t current_icon =
        group_system.GetRaidTargetIndex(target_guid);
    if (current_icon >= 8) {
      return 0;
    }
    icon = current_icon;
    target_guid = 0;
  }

  if (group_system.IsInGroup()) {
    session->interaction().SendRaidTargetUpdate(
        target_guid, static_cast<std::uint8_t>(icon));
    return 0;
  }

  if (target_guid == 0) {
    group_system.SetRaidTarget(0, static_cast<std::uint8_t>(icon));
  } else {
    const std::uint8_t previous_icon =
        group_system.GetRaidTargetIndex(target_guid);
    if (previous_icon < 8) {
      group_system.SetRaidTarget(0, previous_icon);
    }
    if (previous_icon != icon) {
      group_system.SetRaidTarget(target_guid, static_cast<std::uint8_t>(icon));
    }
  }
  ScriptEventDispatch::Get().FireRaidTargetUpdate();
  return 0;
}

int LuaSetRaidRosterSelection(lua_State* L) {
  auto& group_system = ::openwow::game::GroupSystem::Get();
  const auto zero_based_index = ParseRaidRosterSelectionIndex(L);
  if (!zero_based_index.has_value()) {
    group_system.SetRaidRosterSelection(0);
    return 0;
  }
  if (!group_system.IsInRaid()) {
    group_system.SetRaidRosterSelection(0);
    return 0;
  }

  const auto* member = group_system.GetMember(*zero_based_index);
  group_system.SetRaidRosterSelection(member != nullptr ? member->guid : 0);
  return 0;
}

int LuaPromoteToAssistant(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!lua_isstring(L, 1)) {
    luaL_error(L, "Usage: PromoteToAssistant(name)");
  }
  if (!session) {
    return 0;
  }

  const auto exact_match = ScriptReadBoolArgOrDefault(L, 2, false);
  const auto target_guid =
      ResolveGroupPlayerTargetGuid(session, lua_tostring(L, 1), exact_match);
  if (!target_guid.IsEmpty()) {
    session->interaction().SendGroupAssistantLeader(target_guid.GetRawValue(), true);
  }
  return 0;
}

int LuaIsPartyLeader(lua_State* L) {

  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  auto& gs = ::openwow::game::GroupSystem::Get();
  const auto leader_guid = gs.GetLeaderGuid();
  const auto local_guid = session->objects().GetLocalPlayerGuid().GetRawValue();
  if (leader_guid != 0 && local_guid == leader_guid) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsRaidLeader(lua_State* L) {
  auto& gs = ::openwow::game::GroupSystem::Get();
  gs.IsLeader() ? lua_pushnumber(L, 1.0) : lua_pushnil(L);
  return 1;
}

int LuaIsRealPartyLeader(lua_State* L) {
  return PushRealLeaderMatch(L, true);
}

int LuaIsRealRaidLeader(lua_State* L) {
  return PushRealLeaderMatch(L, false);
}

int LuaIsRaidOfficer(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) { lua_pushnil(L); return 1; }
  auto& gs = ::openwow::game::GroupSystem::Get();
  if (!gs.IsInRaid()) { lua_pushnil(L); return 1; }
  auto local_guid = session->objects().GetLocalPlayerGuid();
  (gs.HasRaidOfficerRank(local_guid.GetRawValue()) ? 1 : 0)
      ? lua_pushnumber(L, 1.0)
      : lua_pushnil(L);
  return 1;
}

int LuaRequestRaidInfo(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  session->interaction().SendRequestRaidInfo();
  return 0;
}

int LuaClearPartyAssignment(lua_State* L) {
  return LuaChangePartyAssignment(L, false);
}

int LuaSetPartyAssignment(lua_State* L) {
  return LuaChangePartyAssignment(L, true);
}

int LuaGetPartyAssignment(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!lua_isstring(L, 1)) {
    return luaL_error(
        L,
        "Usage: GetPartyAssignment(\"assignment\" [,\"raidmember\"] [,exactMatch])");
  }

  const auto role = ParsePartyAssignmentRole(lua_tostring(L, 1));
  if (!role.has_value()) {
    return luaL_error(L, "GetPartyAssignment(): Invalid party assignment");
  }

  auto& gs = ::openwow::game::GroupSystem::Get();
  const auto assignment_mask = ResolvePartyAssignmentRoleMask(*role);
  const bool use_raid_roster = gs.IsInRaid();

  if (!session) {
    if (lua_isstring(L, 2)) {
      lua_pushnil(L);
      return 1;
    }
    return 0;
  }

  if (!lua_isstring(L, 2)) {
    return PushPartyAssignmentIndices(L, gs, use_raid_roster, assignment_mask);
  }

  const char* token_or_name = lua_tostring(L, 2);
  const bool exact_match = ReadClientBoolArgOrDefault(L, 3, false);
  const auto guid = ResolveGameUiLookup(
      session, token_or_name ? token_or_name : "",
      ::openwow::game::kTypeMaskPlayer, use_raid_roster ? 6 : 5, exact_match,
      false);
  if (guid.IsEmpty()) {
    DisplaySystemMessage(81, token_or_name ? token_or_name : "");
    return 0;
  }

  const auto raw_guid = guid.GetRawValue();
  const auto flags =
      use_raid_roster
          ? gs.GetMemberFlags(raw_guid)
          : gs.GetTrackedPartyAssignmentFlags(
                raw_guid, session->objects().GetLocalPlayerGuid().GetRawValue());
  lua_pushwowbool(L, (flags & assignment_mask) != 0);
  return 1;
}

int LuaGetPartyLFGBackfillInfo(lua_State* L) {
  auto& group_system = ::openwow::game::GroupSystem::Get();
  if (!group_system.CanPartyLfgBackfill()) {
    return 0;
  }

  const auto* dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto packed_dungeon_id = group_system.GetPartyLfgDungeonId();
  const auto* dungeon = dbc->lfg_dungeons().LookupEntry(packed_dungeon_id & 0x00FFFFFFu);
  if (dungeon == nullptr) {
    return 0;
  }

  lua_pushstring(L, dungeon->name.empty() ? "" : std::string(dungeon->name).c_str());
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->id));
  lua_pushnumber(L, static_cast<lua_Number>(dungeon->type_id));
  return 3;
}

int LuaGetPartyMember(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetPartyMember(1-4)");
  }

  const auto slot =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  if (slot >= 4u) {
    return luaL_error(L, "Usage: GetPartyMember(1-4)");
  }

  const auto& group_system = ::openwow::game::GroupSystem::Get();
  if (group_system.GetTrackedPartyMemberGuid(slot) != 0) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

}

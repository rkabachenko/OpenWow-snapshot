
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/api/game_lua_api_dungeon.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/lfg_system.h"
#include "openwow/net/client_services.h"

#include <optional>
#include <string>

namespace openwow::ui::game::detail {

namespace {

constexpr int kLfgTeleportActionRestrictedMessage = 0x1C2;
constexpr int kLfgTeleportDeadMessage = 0x87;
constexpr int kLfgTeleportFallingMessage = 0x296;
constexpr int kLfgTeleportTransportMessage = 0x88;
constexpr int kLfgTeleportExhaustionMessage = 0x297;

int DenyLfgTeleport(lua_State* L, const int system_message_id) {
  ::openwow::ui::game::DisplaySystemMessage(system_message_id);
  FrameScript_PushBoolean(L, false);
  return 1;
}

int SubmitProposalResponse(lua_State* L, const bool accept) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  if (accept && !GameUI_CanPerformProtectedAction(protected_action_kind::kLookingForGroup)) {
    return 0;
  }

  session->interaction().SendLfgProposalResult(accept);
  return 0;
}

std::optional<std::string> ResolveBootVoteTargetName(
    ::openwow::game::WorldSession* session, std::uint64_t raw_guid) {
  if (session == nullptr || raw_guid == 0) {
    return std::nullopt;
  }

  if (const auto* entry = session->query_cache().GetPlayerName(raw_guid)) {
    if (!entry->name.empty()) {
      std::string name = entry->name;
      if (!entry->realm_name.empty()) {
        name += "-";
        name += entry->realm_name;
      }
      return name;
    }
  }

  return std::nullopt;
}

}

int LuaSetLFGBootVote(lua_State* L) {
  const bool accept = ScriptReadBoolArgOrDefault(L, 1, false);
  auto* session = GetWorldSession(L);
  if (!session) return 0;
  session->interaction().SendLfgBootVote(accept);
  return 0;
}

int LuaGetLFGBootProposal(lua_State* L) {
  auto& lfg = ::openwow::game::LFGSystem::Get();
  const auto boot = lfg.GetBootVoteSnapshot();

  if (!boot.has_value() || !boot->in_progress) {
    lua_pushwowbool(L, false);
    return 1;
  }

  lua_pushwowbool(L, true);
  lua_pushwowbool(L, boot->did_vote);
  lua_pushwowbool(L, boot->my_vote);

  if (const auto target_name =
          ResolveBootVoteTargetName(GetWorldSession(L), boot->target_guid);
      target_name.has_value()) {
    lua_pushstring(L, target_name->c_str());
  } else {
    lua_pushnil(L);
  }

  lua_pushnumber(L, static_cast<lua_Number>(boot->votes_total));
  lua_pushnumber(L, static_cast<lua_Number>(boot->agree_count));
  lua_pushnumber(L, static_cast<lua_Number>(boot->time_left));
  std::string filtered_reason = boot->reason;
  ::openwow::game::ChatFrame_MatureLanguageFilter(filtered_reason, false);
  lua_pushstring(L, filtered_reason.c_str());
  return 8;
}

int LuaGetRandomDungeonBestChoice(lua_State* L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (session == nullptr || dbc == nullptr) {
    return 0;
  }

  const auto best_dungeon_id = session->lfg().GetBestRandomDungeonId(
      *dbc, openwow::net::ClientServices::Instance().GetExpansionLevel());
  if (!best_dungeon_id.has_value()) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(*best_dungeon_id));
  return 1;
}

int LuaAcceptProposal(lua_State* L) {
  return SubmitProposalResponse(L, true);
}

int LuaRejectProposal(lua_State* L) {
  return SubmitProposalResponse(L, false);
}

int LuaLFGTeleport(lua_State* L) {
  auto* session = GetWorldSession(L);
  auto* player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    return 0;
  }

  if (!::openwow::game::LFGSystem::Get().TryPrepareTeleportSend()) {

    return 0;
  }

  if (HasTeleportActionRestriction(*session, *player)) {
    return DenyLfgTeleport(L, kLfgTeleportActionRestrictedMessage);
  }

  if (player->State().IsDead()) {
    return DenyLfgTeleport(L, kLfgTeleportDeadMessage);
  }

  if (player->GetMovementInfo().HasFlag(kMoveFlagFalling)) {
    return DenyLfgTeleport(L, kLfgTeleportFallingMessage);
  }

  if (ActivePlayerHasTransportGuid(*session)) {
    return DenyLfgTeleport(L, kLfgTeleportTransportMessage);
  }

  if (::openwow::game::HasNegativeMirrorTimerScale(0)) {
    return DenyLfgTeleport(L, kLfgTeleportExhaustionMessage);
  }

  const bool teleport_argument = ScriptReadBoolArgOrDefault(L, 1, false);
  session->interaction().SendLfgTeleport(teleport_argument);
  FrameScript_PushBoolean(L, true);
  return 1;
}

int LuaGetLFGDeserterExpiration(lua_State* L) {
  static constexpr std::uint32_t kLfgDeserterAura = 71041;
  return PushLocalPlayerAuraRemainingSecondsIfPresent(
      L, GetWorldSession(L), kLfgDeserterAura);
}

}

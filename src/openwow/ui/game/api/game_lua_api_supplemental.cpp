
extern "C" {
#include <lua.hpp>
}

#include <cstdio>
#include <cstdint>

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/simple_script.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/trainer_system.h"
#include "openwow/game/guild_manager.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/tabard_renderer.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/game/achievements/adapters/lua/achievement_detail_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/game/commerce/auctions/adapters/lua/auction_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_lfg.h"
#include "openwow/game/calendar/adapters/lua/calendar_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_chat.h"
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/commerce/banking/adapters/lua/bank_lua_api.h"
#include "openwow/game/support/knowledge_base/adapters/lua/knowledge_base_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_map.h"
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/ui/game/api/game_lua_api_movement.h"
#include "openwow/ui/game/api/game_lua_api_pet.h"
#include "openwow/ui/game/api/game_lua_api_profession.h"
#include "openwow/ui/game/api/game_lua_api_petition.h"
#include "openwow/ui/game/api/game_lua_api_pvp.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/api/game_lua_api_raid.h"
#include "openwow/game/spells/adapters/lua/spell_power_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_system.h"
#include "openwow/ui/game/api/game_lua_api_talent.h"
#include "openwow/ui/game/api/game_lua_api_unit.h"
#include "openwow/ui/game/api/game_lua_api_vehicle.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"
#include "openwow/ui/game/framescript/core/frame_event_methods.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/threat_warning_state.h"
#include "openwow/ui/game/chat_window_state.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"

namespace openwow::ui::game::detail {

namespace {

constexpr float kNpcInteractionDistancePadding = 4.0f;
constexpr std::uint32_t kPlayerFlagTaxiBenchmarkMode = 0x00020000u;
constexpr std::uint32_t kPlayerFlagPartialPlayTime = 0x00001000u;
constexpr std::uint32_t kPlayerFlagNoPlayTime = 0x00002000u;
constexpr std::uint32_t kTeleportBlockedUnitFlags =
    static_cast<std::uint32_t>(UnitStateFlag::kStunned) |
    static_cast<std::uint32_t>(UnitStateFlag::kFleeing) |
    static_cast<std::uint32_t>(UnitStateFlag::kConfused);

std::string ResolvePendingSummonName(const openwow::game::WorldSession &session) {
  const openwow::game::ObjectGuid summoner_guid{
      session.summon().pending().summoner_guid};
  if (summoner_guid.IsEmpty()) {
    return {};
  }

  if (const auto *summoner = session.objects().Get(summoner_guid);
      summoner != nullptr && summoner->IsUnit()) {
    const std::string name =
        static_cast<const openwow::game::CGUnit_C *>(summoner)->ResolveRetailName(session);
    if (!name.empty()) {
      return name;
    }
  }

  if (const auto *info = session.query_cache().GetPlayerName(summoner_guid.GetRawValue())) {
    return info->name;
  }

  return {};
}

}

bool IsWithinNpcInteractionDistance(const openwow::game::CGPlayer_C &player,
                                    const openwow::game::ObjectManager &object_manager,
                                    const std::uint64_t npc_guid) {
  if (npc_guid == 0) {
    return false;
  }

  const auto *npc = object_manager.GetUnit(openwow::game::ObjectGuid(npc_guid));
  if (npc == nullptr) {
    return false;
  }

  const float dx = npc->GetX() - player.GetX();
  const float dy = npc->GetY() - player.GetY();
  const float dz = npc->GetZ() - player.GetZ();
  const float distance_sq = dx * dx + dy * dy + dz * dz;
  const float threshold = npc->State().GetBoundingRadius() + kNpcInteractionDistancePadding;
  return distance_sq <= threshold * threshold;
}

int LuaApi_AcceptLevelGrant(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->AcceptLevelGrant();
  }
  return 0;
}

int LuaApi_AcceptXPLoss(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  (void)::openwow::game::combat::death::ui::AcceptSpiritHealerXpLoss(*session);
  return 0;
}

int LuaApi_CheckSpiritHealerDist(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnil(L);
    return 1;
  }
  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    lua_pushnil(L);
    return 1;
  }
  const auto healer_guid = session->combat_handler().last_spirit_healer_guid();
  if (healer_guid.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, IsWithinNpcInteractionDistance(*player, session->objects(),
                                                     healer_guid.GetRawValue()));
  return 1;
}

int LuaApi_CheckTalentMasterDist(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *player = session ? session->objects().GetLocalPlayerTyped() : nullptr;
  bool is_in_range = false;
  if (session != nullptr && player != nullptr) {
    const auto &talent_wipe = session->spell_book().last_talent_wipe_confirm();
    is_in_range = talent_wipe.has_value() &&
                  IsWithinNpcInteractionDistance(*player, session->objects(),
                                                 talent_wipe->npc_guid);
  }
  lua_pushwowbool(L, is_in_range);
  return 1;
}

int LuaApi_CloseBattlefield(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  CloseBattlefieldList(*session);
  return 0;
}
int LuaApi_ConfirmBinder(lua_State *L) {

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }
  const auto binder_guid = session->misc().binder_confirm_guid();
  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player != nullptr &&
      IsWithinNpcInteractionDistance(*player, session->objects(), binder_guid)) {
    session->interaction().SendBinderActivate(binder_guid);
  }
  return 0;
}

int LuaApi_ConfirmSummon(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const auto *player = session->objects().GetLocalPlayerTyped();
  if (!player)
    return 0;
  if (player->State().IsDeadOrGhost())
    return 0;

  constexpr std::uint32_t kBlockedSummonFlags =
      static_cast<std::uint32_t>(UnitStateFlag::kStunned) |
      static_cast<std::uint32_t>(UnitStateFlag::kFleeing) |
      static_cast<std::uint32_t>(UnitStateFlag::kConfused) |
      static_cast<std::uint32_t>(UnitStateFlag::kInCombat);
  if ((player->State().GetUnitFlags() & kBlockedSummonFlags) != 0)
    return 0;

  session->interaction().SendSummonResponse(
      session->summon().pending().summoner_guid, true);
  return 0;
}
int LuaApi_DeclineLevelGrant(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->DeclineLevelGrant();
  }
  return 0;
}
int LuaApi_DownloadSettings(lua_State *L) {
  (void)openwow::game::DownloadRuntimeAccountData(
      [](const openwow::net::wotlk::WorldPacket& pkt) {
        return openwow::net::ClientServices__SendPacket(pkt);
      });
  (void)L;
  return 0;
}
int LuaApi_GetCritChanceFromAgility(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetCritChanceFromAgility(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).crit_from_agility());
}

int LuaApi_GetDamageBonusStat(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session != nullptr) {
    if (const auto *player = session->objects().GetActivePlayer(); player != nullptr) {
      const auto *dbc = session->GetDbcLoader();
      if (dbc != nullptr) {
        const auto *chr_class = dbc->chr_classes().LookupEntry(player->State().GetClass());
        if (chr_class != nullptr) {
          lua_pushnumber(L, static_cast<double>(chr_class->primary_stat_type + 1));
          return 1;
        }
      }
    }
  }
  lua_pushnumber(L, 0.0);
  return 1;
}

int LuaApi_GetMirrorTimerInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetMirrorTimerInfo(\"timer\")");
  }

  const int timer_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  if (timer_index < 0 || timer_index > 2) {
    return luaL_error(L, "ERROR: Invalid timer (Ex: \"BREATH\"");
  }

  const auto state = openwow::game::GetMirrorTimerState(timer_index);
  const auto resolved_state = state.value_or(openwow::game::MirrorTimerScriptState{});
  const std::string label =
      openwow::game::ResolveMirrorTimerLabel(resolved_state.type, resolved_state.spell_id);

  lua_pushstring(L, openwow::game::GetMirrorTimerName(resolved_state.type));
  lua_pushnumber(L,
                 static_cast<lua_Number>(static_cast<std::int32_t>(resolved_state.current_value)));
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::int32_t>(resolved_state.max_value)));
  lua_pushnumber(L, static_cast<lua_Number>(resolved_state.scale));
  lua_pushnumber(L, static_cast<lua_Number>(resolved_state.paused));
  lua_pushstring(L, label.c_str());
  return 6;
}

int LuaApi_GetMirrorTimerProgress(lua_State *L) {
  if (!lua_isstring(L, 1)) {

    return luaL_error(L, "Usage: GetMirrorTimerInfo(\"timer\")");
  }

  const int timer_index = openwow::game::GetMirrorTimerIndex(lua_tostring(L, 1));
  if (timer_index > 2) {
    return luaL_error(L, "ERROR: Invalid timer (Ex: \"BREATH\"");
  }

  const auto progress = openwow::game::GetMirrorTimerProgressValue(timer_index);
  lua_pushnumber(L, static_cast<lua_Number>(progress.value_or(0)));
  return 1;
}

int LuaApi_GetMouseButtonName(lua_State *L) {
  if (lua_isnumber(L, 1)) {
    const int ordinal = static_cast<int>(lua_tonumber(L, 1));
    lua_pushstring(L, openwow::ui::widgets::MouseButtonName(
                          openwow::ui::widgets::MouseButtonScriptOrdinalToFlag(ordinal)));
    return 1;
  }

  if (lua_isstring(L, 1)) {
    lua_settop(L, 1);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}
int LuaApi_GetNextStableSlotCost(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *price_entry =
      session != nullptr ? GetNextStableSlotPriceEntry(*session, dbc) : nullptr;
  lua_pushnumber(L, price_entry != nullptr ? static_cast<lua_Number>(price_entry->cost) : 0.0);
  return 1;
}
int LuaApi_GetSummonConfirmAreaName(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (session && dbc) {
    const auto zone_id = session->summon().pending().zone_id;
    if (const auto *entry = dbc->area_table().LookupEntry(zone_id)) {
      if (!entry->name.empty()) {
        lua_pushlstring(L, entry->name.data(), static_cast<size_t>(entry->name.size()));
      } else {
        lua_pushliteral(L, "");
      }
      return 1;
    }
  }
  lua_pushliteral(L, "");
  return 1;
}
int LuaApi_GetSummonConfirmSummoner(lua_State *L) {
  if (const auto *session = GetWorldSession(L)) {
    const auto name = ResolvePendingSummonName(*session);
    lua_pushlstring(L, name.data(), name.size());
    return 1;
  }
  lua_pushliteral(L, "");
  return 1;
}
int LuaApi_GetSummonConfirmTimeLeft(lua_State *L) {
  double seconds_left = 0.0;
  if (const auto *session = GetWorldSession(L)) {
    seconds_left = static_cast<double>(
        session->summon().SecondsRemaining(session->CurrentClientTimeMs()));
  }
  lua_pushnumber(L, seconds_left);
  return 1;
}
int LuaApi_GetTaxiBenchmarkMode(lua_State *L) {
  return PushActivePlayerFlagAsLegacyNumberOrNil(L,
                                                 kPlayerFlagTaxiBenchmarkMode);
}
int LuaApi_GetUnitHealthRegenRateFromSpirit(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetUnitHealthRegenRateFromSpirit(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).health_regen_from_spirit());
}
int LuaApi_HasLFGRestrictions(lua_State *L) {
  lua_pushboolean(
      L, openwow::game::GroupSystem::Get().HasLfgRestrictions() ? 1 : 0);
  return 1;
}
int LuaApi_IsAtStableMaster(lua_State *L) {
  const auto *session = GetWorldSession(L);

  lua_pushwowbool(L,
                  session != nullptr && session->pet().stable_list().npc_guid.GetRawValue() != 0);
  return 1;
}
int LuaApi_IsDesaturateSupported(lua_State *L) {
  if (!TextureStateSupported(L, true)) {
    return 0;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}
int LuaApi_IsInArenaTeam(lua_State *L) {
  const auto *const session = GetWorldSession(L);
  const auto *const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;

  bool belongs_to_arena_team = false;
  if (player != nullptr) {
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
      if (player->GetArenaTeamInfo(slot).team_id != 0u) {
        belongs_to_arena_team = true;
        break;
      }
    }
  }

  lua_pushboolean(L, belongs_to_arena_team ? 1 : 0);
  return 1;
}
int LuaApi_IsThreatWarningEnabled(lua_State *L) {
  if (const auto *session = GetWorldSession(L)) {
    ThreatWarningState::Get().EnsureBinding(session);
  }

  if (ThreatWarningState::Get().IsEnabled()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}
int LuaApi_NoPlayTime(lua_State *L) {
  return PushActivePlayerFlagAsLegacyNumberOrNil(L, kPlayerFlagNoPlayTime);
}
int LuaApi_NotWhileDeadError(lua_State *L) {
  (void)L;
  DisplaySystemMessage(135);
  return 0;
}
int LuaApi_PartialPlayTime(lua_State *L) {
  return PushActivePlayerFlagAsLegacyNumberOrNil(L, kPlayerFlagPartialPlayTime);
}
int LuaApi_PlayerCanTeleport(lua_State *L) {
  const auto *session = GetWorldSession(L);
  const auto *player =
      session ? session->objects().GetLocalPlayerTyped() : nullptr;
  if (!player) {
    lua_pushboolean(L, 0);
    return 1;
  }

  const std::uint32_t unit_flags = player->GetUInt32(UNIT_FIELD_FLAGS);
  lua_pushboolean(L, (unit_flags & kTeleportBlockedUnitFlags) == 0 ? 1 : 0);
  return 1;
}
int LuaApi_RespondInstanceLock(lua_State *L) {

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const bool accept = lua_toboolean(L, 1) != 0;
  const auto pkt = net::wotlk::PacketSender::BuildInstanceLockResponse(accept);
  (void)openwow::net::ClientServices__SendPacket(pkt);
  return 0;
}
int LuaApi_SetLayoutMode(lua_State *L) {

  const int mode = static_cast<int>(luaL_optnumber(L, 1, 1));
  if (auto *manager = openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    manager->retained_layout().SetMode(mode);
  }
  return 0;
}

int LuaApi_SetTaxiBenchmarkMode(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const bool enabled = ScriptReadBoolArgOrDefault(L, 1, true);
  session->interaction().SendSetTaxiBenchmarkMode(enabled);
  return 0;
}
int LuaApi_SetUIVisibility(lua_State *L) {
  const bool enabled = ScriptReadBoolArgOrDefault(L, 1, true);
  GameUI_UpdateNameplateVisibility({.always_show_nameplates = enabled});
  const auto* const session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }
  session->objects().EnumVisibleObjects(
      [enabled](const openwow::game::WorldObject &object) {
        return GameUI_OnUnitHighlightUpdate(object.GetGuid().GetRawValue(), enabled) != 0;
      });
  return 0;
}
int LuaApi_UnitIsControlling(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto uid = UnitIdArg(L, 1);
  const auto *unit = ResolveUnit(session, uid);
  if (!unit) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushwowbool(L, !unit->GetGuidField(UNIT_FIELD_CHARM).IsEmpty());
  return 1;
}
int LuaApi_UnitIsPVPSanctuary(lua_State *L) {
  const LuaCallFrame call{L};
  const auto *const unit =
      ResolveUnitObject(ResolveUnit(call.world_session(), UnitIdArg(L, 1)));
  return call.wow_bool(unit != nullptr && (unit->State().GetPvPFlags() & 0x08u) != 0u);
}
int LuaApi_UnitSwitchToVehicleSeat(lua_State *L) {
  auto *const session = GetWorldSession(L);
  const auto uid = UnitIdArg(L, 1);
  const auto *const unit = ResolveUnitObject(ResolveUnit(session, uid));
  if (session == nullptr || unit == nullptr ||
      unit->GetGuid() != session->objects().GetActivePlayerGuid() ||
      unit->Vehicle().GetVehiclePassengerComponent() == nullptr) {
    return 0;
  }

  const auto *const root_vehicle = openwow::game::ResolveRootVehicleUnit(*unit);
  if (root_vehicle == nullptr) {
    return 0;
  }

  const int expanded_seat = static_cast<int>(lua_tonumber(L, 2)) - 1;
  (void)openwow::game::UnitVehicle_RequestSwitchToSeat(
      *session, unit, root_vehicle, expanded_seat);
  return 0;
}

int LuaApi_UnitVehicleSkin(lua_State *L) {
  static constexpr const char *kSkinNames[] = {"Natural", "Mechanical"};
  const auto *const seat_entry =
      ResolveLuaUnitVehicleSeatEntry(GetWorldSession(L), UnitIdArg(L, 1));
  if (seat_entry == nullptr) {
    return 0;
  }

  const auto skin = seat_entry->temporary_portrait_type;
  if (skin < 2) {
    lua_pushstring(L, kSkinNames[skin]);
  } else {
    lua_pushstring(L, "");
  }
  return 1;
}
int LuaApi_UpdateSpells(lua_State *L) {
  auto* const session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  openwow::game::SpellbookSystem::Get().RefreshDisplayState(
      session->objects());
  ScriptEventDispatch::Get().FireSpellsChanged();
  return 0;
}
int LuaApi_UploadSettings(lua_State *L) {
  openwow::game::AccountDataUploadContext context;
  context.send_packet = [](const openwow::net::wotlk::WorldPacket &pkt) {
    return openwow::net::ClientServices__SendPacket(pkt);
  };
  if (auto *session = GetWorldSession(L); session != nullptr) {
    context.dbc = session->GetDbcLoader();
    context.zone_id = session->objects().GetZoneId();
    context.binding_profiles = session->binding_profiles();
    context.macro_catalog = &session->macros();
  }
  if (auto *runtime_context = runtime::WorldUiRuntimeContext::FromLua(L);
      runtime_context != nullptr) {
    context.world_camera = &runtime_context->world_camera();
    context.retained_layout = &runtime_context->retained_layout();
  }
  (void)openwow::game::UploadRuntimeAccountData(context);
  return 0;
}
}


#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/api/game_lua_api_inspect.h"

#include "openwow/game/talent_info.h"

namespace openwow::ui::game::detail {

namespace {

constexpr float kInspectMaxDistanceSq = 900.0f;

constexpr int kInspectOutOfRangeMessage = 134;
constexpr int kInspectInvalidTargetMessage = 316;
constexpr int kInspectUnknownUnitMessage = 314;
constexpr int kInspectNoTargetMessage = 199;

enum class InspectEligibility {
  kAllowed,
  kInvalidTarget,
  kOutOfRange,
};

InspectEligibility EvaluateInspectTarget(
    const openwow::game::WorldSession& session,
    const openwow::game::CGUnit_C* unit) {
  const auto* active_player = session.objects().GetActivePlayer();
  if (!active_player || !unit || !unit->IsPlayer()) {
    return InspectEligibility::kInvalidTarget;
  }

  if (active_player->Interaction().CanAttackSpellTarget(*unit)) {
    return InspectEligibility::kInvalidTarget;
  }

  const float dx = active_player->GetX() - unit->GetX();
  const float dy = active_player->GetY() - unit->GetY();
  const float dz = active_player->GetZ() - unit->GetZ();
  const float distance_sq = dx * dx + dy * dy + dz * dz;

  return distance_sq > kInspectMaxDistanceSq
             ? InspectEligibility::kOutOfRange
             : InspectEligibility::kAllowed;
}

void ResetInspectTargetState(openwow::game::WorldSession& session,
                             const openwow::game::ObjectGuid target_guid,
                             const openwow::game::CGUnit_C& target_unit) {
  if (session.arena().inspect_target_guid() == target_guid.GetRawValue()) {
    return;
  }

  session.arena().BeginInspect(target_guid.GetRawValue());
  session.inspect().ClearInspectTargetData();
  openwow::game::TalentInfoStore::Get().InitInspectFromGuid(
      target_guid.GetRawValue(), target_unit.State().GetClass(), target_unit.State().GetRace());
}

}

int LuaNotifyInspect(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: NotifyInspect(unit)");
  }

  const char* unit_id = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || !unit_id) return 0;

  auto guid = ResolveUnitId(session, unit_id);
  const auto* target_unit =
      guid.IsEmpty() ? nullptr : session->objects().GetUnit(guid);
  if (EvaluateInspectTarget(*session, target_unit) !=
      InspectEligibility::kAllowed) {
    return 0;
  }

  session->interaction().SendInspect(guid.GetRawValue());
  ResetInspectTargetState(*session, guid, *target_unit);
  return 0;
}

int LuaClearInspectPlayer(lua_State* L) {
  auto* session = GetWorldSession(L);

  if (session && session->arena().inspect_target_guid() != 0) {
    session->arena().ClearInspectState();
    openwow::game::TalentInfoStore::Get().ClearInspectTabArray();
  }
  return 0;
}

int LuaCanInspect(lua_State* L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: CanInspect(unit [, showError])");
  }

  const char* unit_id = lua_tostring(L, 1);
  auto* session = GetWorldSession(L);
  if (!session || !unit_id) {
    lua_pushnil(L);
    return 1;
  }

  auto guid = ResolveUnitId(session, unit_id);
  const bool show_error = ScriptReadBoolArgOrDefault(L, 2, false);
  const auto* target_unit =
      guid.IsEmpty() ? nullptr : session->objects().GetUnit(guid);

  if (show_error) {
    if (target_unit) {
      switch (EvaluateInspectTarget(*session, target_unit)) {
        case InspectEligibility::kAllowed:
          break;
        case InspectEligibility::kInvalidTarget:
          DisplaySystemMessage(kInspectInvalidTargetMessage);
          break;
        case InspectEligibility::kOutOfRange:
          DisplaySystemMessage(kInspectOutOfRangeMessage);
          break;
      }
    } else if (unit_id[0] != '\0' &&
               openwow::core::SStrCmpNoCase(unit_id, "target", 0x7FFFFFFF) != 0) {
      DisplaySystemMessage(kInspectUnknownUnitMessage);
    } else {
      DisplaySystemMessage(kInspectNoTargetMessage);
    }
  }

  const bool can_inspect =
      EvaluateInspectTarget(*session, target_unit) ==
      InspectEligibility::kAllowed;
  lua_pushwowbool(L, can_inspect);
  return 1;
}

int LuaHasInspectHonorData(lua_State* L) {
  if (const auto* session = GetWorldSession(L)) {
    lua_pushwowbool(L, session->arena().HasInspectHonorData());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaRequestInspectHonorData(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  auto& arena = session->arena();
  const auto guid = arena.inspect_target_guid();
  if (guid == 0 || arena.HasInspectHonorData() || arena.IsInspectHonorRequestInFlight()) {
    return 0;
  }

  session->interaction().SendInspectHonorDataRequests(guid);
  arena.MarkInspectHonorRequestSent();
  return 0;
}

int LuaGetInspectHonorData(lua_State* L) {
  const auto* session = GetWorldSession(L);
  const auto honor =
      session ? session->arena().inspect_honor_cache()
              : openwow::game::InspectHonorCache{};

  lua_pushnumber(L, static_cast<lua_Number>(honor.today_honorable_kills));
  lua_pushnumber(L, static_cast<lua_Number>(honor.today_contribution));
  lua_pushnumber(L, static_cast<lua_Number>(honor.yesterday_honorable_kills));
  lua_pushnumber(L, static_cast<lua_Number>(honor.yesterday_contribution));
  lua_pushnumber(L, static_cast<lua_Number>(honor.lifetime_honorable_kills));
  lua_pushnumber(L, static_cast<lua_Number>(honor.lifetime_rank));
  return 6;
}

}

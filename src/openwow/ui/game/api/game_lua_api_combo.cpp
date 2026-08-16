
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_combo.h"

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kRogueClassId = 4u;
constexpr std::uint8_t kDruidClassId = 11u;

ObjectGuid ResolveComboTargetGuid(WorldSession &session, const std::string &target_unit_id) {
  if (!target_unit_id.empty()) {
    const ObjectGuid resolved = ResolveUnitId(&session, target_unit_id);
    if (!resolved.IsEmpty()) {
      return resolved;
    }
  }

  return session.objects().GetTargetGuid();
}

int GetActivePlayerComboPoints(const CGPlayer_C &player, const ObjectGuid target_guid) {
  if (player.State().GetClass() != kRogueClassId && player.State().GetClass() != kDruidClassId) {
    return 0;
  }

  return player.Casts().GetComboTarget() == target_guid ? player.GetComboPoints() : 0;
}

int GetActivePetComboPoints(const WorldSession &session, const ObjectGuid target_guid) {
  const auto &pet_combo_points = session.pet_handler().last_pet_combo_points();
  if (!pet_combo_points.has_value()) {
    return 0;
  }

  return pet_combo_points->target_guid == target_guid.GetRawValue()
             ? pet_combo_points->combo_points
             : 0;
}

}

int LuaGetComboPoints(lua_State* L) {
  if (lua_isstring(L, 1) == 0) {

    return luaL_error(L, "Usage: GetComboPoints(\"unit\"[, \"target\"]");
  }

  const std::string unit_id = SafeLuaString(L, 1);
  const std::string target_unit_id = SafeLuaString(L, 2);

  auto* session = GetWorldSession(L);
  if (!session) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  const auto* player = session->objects().GetActivePlayer();
  if (!player) {
    FrameScript_PushNumberFromInt(L, 0);
    return 1;
  }

  const ObjectGuid unit_guid = ResolveUnitId(session, unit_id);
  const ObjectGuid target_guid = ResolveComboTargetGuid(*session, target_unit_id);

  int combo_points = 0;
  if (unit_guid == player->GetGuid()) {
    combo_points = GetActivePlayerComboPoints(*player, target_guid);
  } else if (!player->State().GetPetGUID().IsEmpty() && unit_guid == player->State().GetPetGUID()) {
    combo_points = GetActivePetComboPoints(*session, target_guid);
  }

  FrameScript_PushNumberFromInt(L, combo_points);
  return 1;
}

}


#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_aura.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/aura_tracker.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/unit_vehicle.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"

#include <algorithm>
#include <optional>
#include <string>

namespace openwow::ui::game::detail {

int LuaCancelUnitBuff(lua_State* L) {
  const int selector_type = lua_gettop(L) >= 2 ? lua_type(L, 2) : LUA_TNONE;
  if (!lua_isstring(L, 1) ||
      (selector_type != LUA_TNONE && selector_type != LUA_TNUMBER &&
       selector_type != LUA_TSTRING)) {
    return luaL_error(
        L,
        "Usage: CancelUnitBuff(\"unit\", [index] or [\"name\", \"rank\"][, \"filter\"])");
  }

  auto* session = GetWorldSession(L);
  if (!session) return 0;

  auto uid = SafeLuaString(L, 1);
  auto guid = ResolveUnitId(session, uid);
  if (guid.IsEmpty()) return 0;

  auto& aura_bridge = ::openwow::game::AuraLuaBridge::Get();
  std::string aura_name;
  std::string rank;
  std::string filter;
  std::uint32_t index = 1u;
  if (selector_type == LUA_TSTRING) {
    aura_name = SafeLuaString(L, 2);
    rank = lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TSTRING
               ? SafeLuaString(L, 3)
               : "";
    filter =
        lua_gettop(L) >= 4 && lua_type(L, 4) == LUA_TSTRING
            ? SafeLuaString(L, 4)
            : "HELPFUL";
  } else {
    index = lua_gettop(L) >= 2
        ? static_cast<std::uint32_t>(lua_tonumber(L, 2))
        : 1u;
    if (index == 0u) {
      return 0;
    }
    filter =
        lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TSTRING
            ? SafeLuaString(L, 3)
            : "HELPFUL";
  }

  const auto select_aura = [&](const ::openwow::game::ObjectGuid unit_guid) {
    return selector_type == LUA_TSTRING
               ? aura_bridge.FindUnitAura(*session, unit_guid, aura_name, rank,
                                          filter)
               : aura_bridge.GetUnitAura(*session, unit_guid, index, filter);
  };

  const auto local_player_guid = session->objects().GetLocalPlayerGuid();
  if (guid != local_player_guid) {

    if (const auto *const active_player = session->objects().GetActivePlayer();
        active_player != nullptr) {

      const auto pet_guid = active_player->GetGuidField(UNIT_FIELD_SUMMON);
      const auto charmed_guid = active_player->GetGuidField(UNIT_FIELD_CHARM);
      const auto owned_pet_guid =
          pet_guid.GetRawValue() != 0 ? pet_guid : charmed_guid;
      if (owned_pet_guid.GetRawValue() != 0 && guid == owned_pet_guid) {
        const auto selected = select_aura(guid);
        if (!selected.has_value()) {
          return 0;
        }
        const auto *const raw_pet_aura =
            session->aura().FindAuraBySpellId(guid.GetRawValue(), selected->spellId);
        const auto *const pet_dbc = session->GetDbcLoader();
        const auto *const pet_spell = pet_dbc != nullptr
            ? pet_dbc->spell().LookupEntry(selected->spellId)
            : nullptr;
        const bool pet_channelled_aura =
            pet_spell != nullptr && (pet_spell->attributes_ex & 0x00000004u) != 0u;
        if (raw_pet_aura != nullptr &&
            (::openwow::game::HasFlag(raw_pet_aura->flags,
                                      ::openwow::game::AuraFlag::kPositive) ||
             pet_channelled_aura)) {
          session->interaction().SendPetCancelAura(guid.GetRawValue(), selected->spellId);
        }
        return 0;
      }
    }

    const auto active_mover_guid =
        session->player_control_runtime().ActiveMoverGuid();
    const auto* const active_mover =
        session->objects().GetUnit(active_mover_guid);
    const auto* const active_player = session->objects().GetActivePlayer();
    if (active_mover == nullptr || active_player == nullptr ||
        !::openwow::game::CanUseVehicleControlAction(
            *session, ::openwow::game::VehicleControlSeatFlag::kCanExit)) {
      return 0;
    }

    const auto selected = select_aura(active_mover_guid);
    const auto* const spell = selected.has_value() && session->GetDbcLoader()
        ? session->GetDbcLoader()->spell().LookupEntry(selected->spellId)
        : nullptr;
    const bool is_player_vehicle_control_aura =
        selected.has_value() && selected->casterGuid == local_player_guid &&
        spell != nullptr &&
        std::find(spell->effect_apply_aura.begin(),
                  spell->effect_apply_aura.end(), 236u) !=
            spell->effect_apply_aura.end();
    const auto* const passenger =
        active_player->Vehicle().GetVehiclePassengerComponent();
    if (is_player_vehicle_control_aura && passenger != nullptr &&
        passenger->GetVehicleUnitGuid() == active_mover_guid.GetRawValue()) {
      ::openwow::game::UnitVehicle_RequestExit(*session, active_mover);
    }
    return 0;
  }

  const auto selected = select_aura(guid);
  if (!selected.has_value()) return 0;

  const auto* const raw_aura =
      session->aura().FindAuraBySpellId(guid.GetRawValue(), selected->spellId);
  const auto* const dbc = session->GetDbcLoader();
  const auto* const spell = dbc != nullptr
      ? dbc->spell().LookupEntry(selected->spellId)
      : nullptr;
  const bool channelled_aura =
      spell != nullptr && (spell->attributes_ex & 0x00000004u) != 0u;
  if (raw_aura != nullptr &&
      (::openwow::game::HasFlag(raw_aura->flags,
                                ::openwow::game::AuraFlag::kPositive) ||
       channelled_aura)) {
    session->interaction().SendCancelAura(selected->spellId);
  }

  return 0;
}

}

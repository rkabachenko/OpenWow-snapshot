
#include "openwow/ui/game/api/game_lua_api_internal.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/core/storm_string.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
#include <lua.hpp>
#include <lua.hpp>
}

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace openwow::ui::game::detail {

namespace {

void PushTrainerStepReqDefault(lua_State *L) {
  lua_pushnil(L);
  lua_pushnumber(L, 1.0);
}

std::optional<std::int32_t>
FindPlayerSkillRankWithStepModifier(const openwow::game::CGPlayer_C &player,
                                    std::uint16_t skill_line_id) {
  const auto skill_values = player.FindActiveSkillValues(skill_line_id);
  if (!skill_values.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(skill_values->adjusted_value);
}

bool IsAllTrainerServiceTypeFilter(const char *filter_name) {
  return filter_name != nullptr &&
         openwow::core::SStrCmpNoCase(filter_name, "all", 0x7FFFFFFFu) == 0;
}

std::int32_t ReadTrainerSkillLineIndexOrUsage(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, usage);
  }

  return TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
}

}

static const openwow::data::dbc::DbcLoader *GetDbcTrainer(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

const openwow::game::TrainerSpell *ResolveVisibleTrainerSpell(lua_State *L, const int index) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  if (!session || !session->gossip().has_trainer() || !dbc) {
    return nullptr;
  }

  const auto spell_index = openwow::game::Trainer_ResolveVisibleSpellIndex(
      *session, dbc, static_cast<std::uint32_t>(index));
  if (!spell_index) {
    return nullptr;
  }

  const auto &trainer = session->gossip().trainer();
  if (*spell_index >= trainer.spells.size()) {
    return nullptr;
  }
  return &trainer.spells[*spell_index];
}

const openwow::game::TrainerSpell *ResolveTrainerSpellFromPacketOrder(const openwow::game::WorldSession &session,
                                                                      const int one_based_index) {
  if (!session.gossip().has_trainer() || one_based_index <= 0) {
    return nullptr;
  }

  const auto &trainer_spells = session.gossip().trainer().spells;
  const auto raw_index = static_cast<std::size_t>(one_based_index - 1);
  if (raw_index >= trainer_spells.size()) {
    return nullptr;
  }

  return &trainer_spells[raw_index];
}

const openwow::game::TrainerSpell *ResolveTrainerServiceSpell(lua_State *L,
                                                              const int one_based_index) {
  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_trainer()) {
    return nullptr;
  }

  if (const auto *spell = ResolveVisibleTrainerSpell(L, one_based_index); spell != nullptr) {
    return spell;
  }

  if (GetDbcTrainer(L) != nullptr) {
    return nullptr;
  }

  return ResolveTrainerSpellFromPacketOrder(*session, one_based_index);
}

static constexpr std::int32_t kTrainerTypeTrade = 2;

int LuaGetNumTrainerServices(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  if (!session || !session->gossip().has_trainer() || !dbc) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(
      L, static_cast<lua_Integer>(openwow::game::Trainer_GetVisibleServiceCount(*session, dbc)));
  return 1;
}

int LuaGetTrainerServiceInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceInfo(index)");
  }

  const auto one_based_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  if (!session || !session->gossip().has_trainer() || !dbc) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    return 4;
  }

  openwow::game::TrainerServiceInfoView view;
  if (one_based_index < 1 ||
      !openwow::game::Trainer_GetServiceInfo(*session, dbc,
                                             static_cast<std::uint32_t>(one_based_index), &view)) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnumber(L, 1.0);
    return 4;
  }

  if (view.name.empty()) {
    lua_pushnil(L);
  } else {
    lua_pushlstring(L, view.name.data(), static_cast<size_t>(view.name.size()));
  }
  if (view.subtext.empty()) {
    lua_pushstring(L, "");
  } else {
    lua_pushlstring(L, view.subtext.data(), static_cast<size_t>(view.subtext.size()));
  }
  lua_pushlstring(L, view.availability.data(), static_cast<size_t>(view.availability.size()));
  if (view.show_numeric_flag) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 4;
}

int LuaGetTrainerServiceCost(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceCost(index)");
  }

  const auto one_based_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *spell = ResolveTrainerServiceSpell(L, one_based_index);
  if (!spell) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 3;
  }
  lua_pushnumber(L, static_cast<lua_Integer>(spell->money_cost));
  lua_pushnumber(L, static_cast<lua_Integer>(spell->point_cost_0));
  lua_pushnumber(L, static_cast<lua_Integer>(spell->point_cost_1));
  return 3;
}

int LuaGetTrainerServiceLevelReq(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceLevelReq(index)");
  }

  const auto one_based_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *spell = ResolveTrainerServiceSpell(L, one_based_index);
  if (!spell) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Integer>(spell->req_level));
  return 1;
}

int LuaGetTrainerServiceSkillReq(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceSkillReq(index)");
  }

  const auto one_based_index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  const auto *spell = ResolveVisibleTrainerSpell(L, one_based_index);
  const auto *active_player = session != nullptr ? session->objects().GetActivePlayer() : nullptr;

  if (!spell || !dbc || !active_player || spell->req_skill_line == 0 ||
      spell->req_skill_rank == 0) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1.0);
    return 3;
  }

  const auto *skill_line =
      dbc->skill_line().LookupEntry(static_cast<std::uint32_t>(spell->req_skill_line));
  if (!skill_line || skill_line->name.empty()) {
    lua_pushnil(L);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1.0);
    return 3;
  }

  lua_pushlstring(L, skill_line->name.data(), static_cast<size_t>(skill_line->name.size()));
  lua_pushnumber(L, static_cast<lua_Integer>(spell->req_skill_rank));

  const auto player_skill_rank =
      spell->req_skill_line > std::numeric_limits<std::uint16_t>::max()
          ? std::optional<std::int32_t>{}
          : FindPlayerSkillRankWithStepModifier(*active_player,
                                                static_cast<std::uint16_t>(spell->req_skill_line));
  if (player_skill_rank && *player_skill_rank >= spell->req_skill_rank) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 3;
}

int LuaGetTrainerServiceSkillLine(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  if (!session || !session->gossip().has_trainer() || !dbc) {
    lua_pushnil(L);
    return 1;
  }

  const auto skill_line_name = openwow::game::Trainer_GetVisibleServiceSkillLineName(
      *session, dbc, static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
  if (!skill_line_name) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushlstring(L, skill_line_name->data(), static_cast<size_t>(skill_line_name->size()));
  return 1;
}

int LuaGetTrainerServiceIcon(lua_State *L) {
  const auto *spell = ResolveVisibleTrainerSpell(L, static_cast<int>(luaL_checkinteger(L, 1)));
  const auto *dbc = GetDbcTrainer(L);
  if (!spell || !dbc) {
    lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
    return 1;
  }
  const auto *spell_entry = dbc->spell().LookupEntry(static_cast<std::uint32_t>(spell->spell_id));
  if (spell_entry && spell_entry->spell_icon_id > 0) {
    const auto *icon = dbc->spell_icon().LookupEntry(spell_entry->spell_icon_id);
    if (icon && !icon->icon_path.empty()) {
      lua_pushstring(L, std::string(icon->icon_path).c_str());
      return 1;
    }
  }

  lua_pushstring(L, "Interface\\Icons\\INV_Misc_QuestionMark");
  return 1;
}

int LuaGetTrainerServiceTypeFilter(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceTypeFilter(\"type\")");
  }

  const auto *filter_name = lua_tostring(L, 1);
  const auto filter_bucket = openwow::game::Trainer_ParseFilterString(filter_name);
  if (filter_bucket == 6) {
    return luaL_error(L, "Bad service type in GetTrainerServiceTypeFilter");
  }

  if (openwow::game::Trainer_GetServiceTypeFilterEnabled(filter_bucket)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaSetTrainerServiceTypeFilter(lua_State *L) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L,
                      "Usage: SetTrainerServiceTypeFilter(\"type\", on/off [, exclusive])");
  }

  const auto *filter_name = lua_tostring(L, 1);
  if (IsAllTrainerServiceTypeFilter(filter_name)) {
    openwow::game::Trainer_ResetServiceTypeFilters();
    return 0;
  }

  const auto filter_bucket = openwow::game::Trainer_ParseFilterString(filter_name);
  if (filter_bucket == 6) {
    return luaL_error(L, "Bad service type in SetTrainerServiceTypeFilter");
  }
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "Missing on/off parameter in SetTrainerServiceTypeFilter");
  }

  const bool enabled = TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) != 0;
  const bool exclusive =
      lua_isnumber(L, 3) != 0 && TruncateLuaNumberToSseI32(lua_tonumber(L, 3)) != 0;
  openwow::game::Trainer_SetServiceTypeFilter(filter_bucket, enabled, exclusive);
  return 0;
}

int LuaGetTrainerGreetingText(lua_State *L) {
  const auto &greeting = openwow::game::Trainer_GetGreetingText();
  lua_pushstring(L, greeting.c_str());
  return 1;
}

int LuaIsTradeskillTrainer(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushwowbool(L, session->gossip().trainer_type() == kTrainerTypeTrade);
  return 1;
}

int LuaCloseTrainer(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session != nullptr && session->gossip().has_trainer()) {
    CloseTrainerInteraction(
        *session, session->gossip().trainer().trainer_guid,
        NpcInteractionClosureCause::UnitUnavailable);

    openwow::game::Trainer_ResetFrameState();
  }
  return 0;
}

int LuaBuyTrainerServiceApi(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: BuyTrainerService(index)");
  }

  auto *session = GetWorldSession(L);
  if (!session || !session->gossip().has_trainer())
    return 0;

  const int index = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  const auto &trainer = session->gossip().trainer();

  if (index >= 1) {

    if (!openwow::game::Trainer_IsVisibleServiceAvailable(
            *session, GetDbcTrainer(L), static_cast<std::uint32_t>(index))) {
      return 0;
    }
    const auto *spell = ResolveVisibleTrainerSpell(L, index);
    if (!spell) return 0;
    session->interaction().SendTrainerBuySpell(trainer.trainer_guid.GetRawValue(),
                                               static_cast<std::uint32_t>(spell->spell_id));
  } else {

    const auto *dbc = GetDbcTrainer(L);
    if (dbc == nullptr) {
      return 0;
    }
    const auto visible_count = openwow::game::Trainer_GetVisibleServiceCount(*session, dbc);
    for (std::uint32_t one_based_index = 1; one_based_index <= visible_count;
         ++one_based_index) {
      if (!openwow::game::Trainer_IsVisibleServiceAvailable(
              *session, dbc, one_based_index)) {
        continue;
      }
      const auto *spell = ResolveVisibleTrainerSpell(L, static_cast<int>(one_based_index));
      if (spell == nullptr) {
        continue;
      }
      session->interaction().SendTrainerBuySpell(trainer.trainer_guid.GetRawValue(),
                                                  static_cast<std::uint32_t>(spell->spell_id));
    }
  }
  return 0;
}

int LuaIsTrainerServiceSkillStep(lua_State *L) {
  const auto *dbc = GetDbcTrainer(L);
  const auto *trainer_spell =
      ResolveVisibleTrainerSpell(L, static_cast<int>(luaL_checkinteger(L, 1)));
  if (!trainer_spell || !dbc) {
    lua_pushnil(L);
    return 1;
  }
  if (trainer_spell->spell_id <= 0) {
    lua_pushnil(L);
    return 1;
  }

  if (!openwow::game::Trainer_HasSkillStepEffect(*dbc, trainer_spell->spell_id)) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, 1.0);
  return 1;
}

int LuaGetTrainerServiceStepReq(lua_State *L) {
  (void)luaL_checkinteger(L, 1);
  PushTrainerStepReqDefault(L);
  return 2;
}

int LuaGetTrainerSkillLineFilter(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  const auto index =
      ReadTrainerSkillLineIndexOrUsage(L, "Usage: GetTrainerSkillLineFilter(index)");
  const auto skill_line_count =
      session != nullptr ? openwow::game::Trainer_GetSkillLineCount(*session, dbc) : 0u;
  if (index > 0 && static_cast<std::uint32_t>(index) > skill_line_count) {
    return luaL_error(L, "Bad skill line in GetTrainerSkillLineFilter");
  }

  const bool enabled = session != nullptr
                           ? openwow::game::Trainer_GetSkillLineFilterEnabled(*session, dbc, index)
                           : index <= 0;
  if (enabled) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaSetTrainerSkillLineFilter(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  const auto index =
      ReadTrainerSkillLineIndexOrUsage(L,
                                       "Usage: SetTrainerSkillLineFilter(index [, on\\off, exclusive])");

  if (index <= 0) {
    if (session != nullptr) {
      openwow::game::Trainer_SetSkillLineFilter(*session, dbc, index, true, false);
    }
    return 0;
  }

  const auto skill_line_count =
      session != nullptr ? openwow::game::Trainer_GetSkillLineCount(*session, dbc) : 0u;
  if (static_cast<std::uint32_t>(index) > skill_line_count) {
    return luaL_error(L, "Bad skill line in SetTrainerSkillLineFilter");
  }
  if (!lua_isnumber(L, 2)) {
    return luaL_error(L, "Missing on//off parameter in SetTrainerSkillLineFilter");
  }

  const bool enabled = TruncateLuaNumberToSseI32(lua_tonumber(L, 2)) != 0;
  const bool exclusive =
      lua_isnumber(L, 3) != 0 && TruncateLuaNumberToSseI32(lua_tonumber(L, 3)) != 0;
  if (session != nullptr &&
      !openwow::game::Trainer_SetSkillLineFilter(*session, dbc, index, enabled, exclusive)) {
    return luaL_error(L, "Bad skill line in SetTrainerSkillLineFilter");
  }
  return 0;
}

int LuaGetTrainerSkillLines(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcTrainer(L);
  if (!session || !session->gossip().has_trainer() || !dbc) {
    return 0;
  }

  const auto count = openwow::game::Trainer_GetSkillLineCount(*session, dbc);
  for (std::uint32_t index = 1; index <= count; ++index) {
    const auto skill_line_name = openwow::game::Trainer_GetSkillLineName(*session, dbc, index);
    if (skill_line_name) {
      lua_pushlstring(L, skill_line_name->data(), static_cast<size_t>(skill_line_name->size()));
    } else {
      lua_pushnil(L);
    }
  }
  return static_cast<int>(count);
}

}

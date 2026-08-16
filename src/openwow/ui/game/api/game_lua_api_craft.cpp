
#include "openwow/ui/game/api/game_lua_api_craft.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/localization.h"
#include "openwow/game/skill_info.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_profession.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/lua_numeric.h"

#include <limits>
#include <string>

namespace openwow::ui::game::detail {

namespace {

constexpr std::int32_t kTrainerTypeTrade = 2;

constexpr std::int32_t kSseI32IntegerIndefinite =
    std::numeric_limits<std::int32_t>::min();

void PushTrainerAbilityReqDefault(lua_State *L) {
  lua_pushnil(L);
  lua_pushnumber(L, 1.0);
}

std::uint32_t FindTrainerAbilityNextRankSpell(const openwow::data::dbc::DbcLoader &dbc,
                                              std::uint32_t spell_id) {
  for (const auto &entry : dbc.skill_line_ability().entries()) {
    if (entry.spell_id == spell_id) {
      return entry.superseded_by_spell;
    }
  }
  return 0;
}

bool PlayerKnowsTrainerAbilitySpell(const openwow::game::WorldSession &session,
                                    const openwow::data::dbc::DbcLoader &dbc,
                                    std::uint32_t spell_id) {
  std::uint32_t current_spell = spell_id;
  while (current_spell != 0) {
    if (session.spell_book().HasSpell(current_spell)) {
      return true;
    }

    const auto next_spell = FindTrainerAbilityNextRankSpell(dbc, current_spell);
    if (next_spell == current_spell) {
      break;
    }
    current_spell = next_spell;
  }

  return false;
}

std::string BuildTrainerAbilityRequirementLabel(const openwow::data::dbc::DbcLoader &dbc,
                                                std::int32_t spell_id) {
  if (spell_id <= 0) {
    return {};
  }

  const auto *spell = dbc.spell().LookupEntry(static_cast<std::uint32_t>(spell_id));
  if (!spell || spell->spell_name.empty()) {
    return {};
  }

  std::string label(spell->spell_name);
  if (!spell->rank.empty()) {
    label += " (";
    label += spell->rank;
    label += ')';
  }
  return label;
}

const openwow::game::TrainerSpell *ResolveVisibleTrainerSpell(lua_State *L, const int index) {
  auto *session = GetWorldSession(L);
  auto *dbc = GetDbcLoader(L);
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

void FireTrainerDescriptionUpdateOnAsyncItemTemplateSuccess(const bool success) {
  if (!success) {
    return;
  }

  ScriptEventDispatch::Get().FireEvent(events::TRAINER_DESCRIPTION_UPDATE);
}

std::int32_t ReadTrainerIndexOrUsage(lua_State *L, const char *usage) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, usage);
  }

  return TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
}

std::string FormatTrainerStepIncrease(lua_State *L, const std::string_view skill_name,
                                      const std::uint32_t increase) {
  auto format = openwow::game::ResolveLocalizedGlobalString(L, "INCREASE_POTENTIAL");
  if (format.empty()) {
    format = "Increases potential in |cffffffff%s|r by |cffffffff%d|r";
  }

  return openwow::game::Localization::Get().FormatString(
      format, {std::string(skill_name), std::to_string(increase)});
}

}

int LuaSelectTradeSkill(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SelectTradeSkill(index)");
  }

  SelectTradeSkillByLuaIndexState(TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  return 0;
}

int LuaSetTradeSkillItemNameFilter(lua_State *L) {
  SetTradeSkillItemNameFilterState(lua_tostring(L, 1));
  return 0;
}

int LuaGetTradeSkillItemNameFilter(lua_State *L) {
  lua_pushstring(L, GetTradeSkillItemNameFilterState());
  return 1;
}

int LuaSetTradeSkillItemLevelFilter(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
    return luaL_error(L, "Usage: SetTradeSkillItemLevelFilter(minLevel, maxLevel)");
  }

  const int max_level = TruncateLuaNumberToSseI32(lua_tonumber(L, 2));
  const int min_level = TruncateLuaNumberToSseI32(lua_tonumber(L, 1));
  SetTradeSkillItemLevelFilterState(min_level, max_level);
  return 0;
}

int LuaGetTradeSkillItemLevelFilter(lua_State *L) {
  lua_pushnumber(L, GetTradeSkillItemLevelFilterMinState());
  lua_pushnumber(L, GetTradeSkillItemLevelFilterMaxState());
  return 2;
}

int LuaTradeSkillOnlyShowMakeable(lua_State *L) {
  const bool enabled = lua_isnone(L, 1) != 0 || ReadClientBoolArgOrDefault(L, 1, true);
  SetTradeSkillOnlyShowMakeableState(enabled);
  return 0;
}

int LuaTradeSkillOnlyShowSkillUps(lua_State *L) {
  const bool enabled = lua_isnone(L, 1) != 0 || ReadClientBoolArgOrDefault(L, 1, true);
  SetTradeSkillOnlyShowSkillUpsState(enabled);
  return 0;
}

int LuaGetTradeskillRepeatCount(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Integer>(GetTradeSkillRepeatCountState()));
  return 1;
}

int LuaGetTrainerSelectionIndex(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = GetDbcLoader(L);
  const auto selected_index = openwow::game::Trainer_GetSelectionIndex(*session, dbc);
  if (!selected_index) {
    lua_pushnil(L);
  } else {
    lua_pushnumber(L, static_cast<lua_Integer>(*selected_index));
  }
  return 1;
}

int LuaGetTrainerServiceAbilityReq(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (!session || !session->gossip().has_trainer() || !dbc ||
      !session->objects().GetActivePlayer()) {
    PushTrainerAbilityReqDefault(L);
    return 2;
  }

  const int index = static_cast<int>(luaL_checkinteger(L, 1));
  const int req_index = static_cast<int>(luaL_checkinteger(L, 2));
  const auto *trainer_spell = ResolveVisibleTrainerSpell(L, index);
  if (!trainer_spell || req_index < 1 || req_index > 3) {
    PushTrainerAbilityReqDefault(L);
    return 2;
  }

  const auto required_spell_id = trainer_spell->req_abilities[req_index - 1];
  const auto label = BuildTrainerAbilityRequirementLabel(*dbc, required_spell_id);
  if (label.empty()) {
    PushTrainerAbilityReqDefault(L);
    return 2;
  }

  lua_pushstring(L, label.c_str());
  if (PlayerKnowsTrainerAbilitySpell(*session, *dbc,
                                     static_cast<std::uint32_t>(required_spell_id))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaGetTrainerServiceDescription(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceDescription(index)");
  }

  const auto trainer_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  const auto *trainer_spell = ResolveVisibleTrainerSpell(L, static_cast<int>(trainer_index));
  if (trainer_spell == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  if (trainer_spell->spell_id > 0) {
    const auto spell_id = static_cast<std::uint32_t>(trainer_spell->spell_id);
    const auto spell_description =
        openwow::game::ResolveSpellDescriptionForDisplay(
            spell_id,
            openwow::game::SpellQueryBridge::Get().GetSpellDescription(spell_id));
    if (!spell_description.empty()) {
      lua_pushstring(L, spell_description.c_str());
      return 1;
    }
  }

  if (session != nullptr && dbc != nullptr && trainer_spell->spell_id > 0) {
    const auto item_id =
        openwow::game::Trainer_ResolveVisibleServiceCreatedItemId(*session, dbc, trainer_index);
    if (item_id.has_value()) {
      const auto *item_template = session->query_cache().GetOrRequestItemTemplate(
          *item_id, openwow::game::QueryCache::QueryRequestOptions{

                        .dedupe_callbacks = false,
                        .callback = FireTrainerDescriptionUpdateOnAsyncItemTemplateSuccess,
                    });
      if (item_template != nullptr && !item_template->description.empty()) {
        lua_pushstring(L, item_template->description.c_str());
        return 1;
      }
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetTrainerServiceItemLink(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetTrainerServiceItemLink(index)");
  }

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (session == nullptr || dbc == nullptr || !session->gossip().has_trainer()) {
    lua_pushnil(L);
    return 1;
  }

  const auto trainer_index = static_cast<std::uint32_t>(
      TruncateLuaNumberToSseI32(lua_tonumber(L, 1)));
  if (session->gossip().trainer_type() != kTrainerTypeTrade) {
    lua_pushnil(L);
    return 1;
  }

  const auto item_id =
      openwow::game::Trainer_ResolveVisibleServiceCreatedItemId(*session, dbc, trainer_index);
  if (!item_id.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  const auto *item_template = session->query_cache().GetOrRequestItemTemplate(*item_id);
  if (item_template == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto color_info = openwow::game::ItemTemplate::GetQualityColorInfo(
      static_cast<std::uint32_t>(item_template->quality));
  const std::string link = std::string(color_info.hyperlink_color) +
                           "|Hitem:" + std::to_string(*item_id) + ":0:0:0:0:0:0:0|h[" +
                           item_template->name + "]|h|r";
  lua_pushstring(L, link.c_str());
  return 1;
}

int LuaGetTrainerServiceNumAbilityReq(lua_State *L) {
  const auto *trainer_spell =
      ResolveVisibleTrainerSpell(L, static_cast<int>(luaL_checkinteger(L, 1)));
  if (!trainer_spell) {
    lua_pushnumber(L, 0);
    return 1;
  }
  int count = 0;
  for (const auto required_spell_id : trainer_spell->req_abilities) {
    if (required_spell_id > 0) {
      ++count;
    }
  }

  lua_pushnumber(L, count);
  return 1;
}

int LuaGetTrainerServiceStepIncrease(lua_State *L) {
  const auto one_based_index =
      ReadTrainerIndexOrUsage(L, "Usage: GetTrainerServiceStepIncrease(index)");
  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (session == nullptr || dbc == nullptr || !session->gossip().has_trainer()) {
    lua_pushnil(L);
    return 1;
  }

  const auto *active_player = session->objects().GetActivePlayer();
  const auto *trainer_spell = ResolveVisibleTrainerSpell(L, one_based_index);
  if (active_player == nullptr || trainer_spell == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto increase =
      openwow::game::Trainer_ResolveServiceStepIncrease(*dbc, *trainer_spell,
                                                        active_player->State().GetLevel());
  if (!increase.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  const auto text = FormatTrainerStepIncrease(L, increase->skill_name, increase->rank_increase);
  if (text.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, text.c_str());
  return 1;
}

int LuaOpenTrainer(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto *active_player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (active_player != nullptr) {
    RequestTrainerInteraction(*session, active_player->GetGuid());
  }
  return 0;
}

int LuaSelectTrainerService(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto one_based_index =
      ReadTrainerIndexOrUsage(L, "Usage: SelectTrainerService(index)");
  if (session != nullptr) {
    openwow::game::Trainer_SelectVisibleService(*session, GetDbcLoader(L),
                                                static_cast<std::uint32_t>(one_based_index));
  }
  return 0;
}

int LuaCollapseTrainerSkillLine(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *dbc = GetDbcLoader(L);
  const int index = ReadTrainerIndexOrUsage(L, "Usage: CollapseTrainerSkillLine(index)");
  if (index == kSseI32IntegerIndefinite) {
    return luaL_error(L, "Bad skill line in CollapseTrainerSkillLine");
  }
  const bool collapsed =
      session != nullptr ? openwow::game::Trainer_CollapseSkillLine(*session, dbc, index)
                         : openwow::game::Trainer_CollapseSkillLine(index);
  if (!collapsed) {
    return luaL_error(L, "Bad skill line in CollapseTrainerSkillLine");
  }
  return 0;
}

int LuaExpandTrainerSkillLine(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *dbc = GetDbcLoader(L);
  const int index = ReadTrainerIndexOrUsage(L, "Usage: ExpandTrainerSkillLine(index)");
  if (!session) {
    return 0;
  }
  if (!openwow::game::Trainer_ExpandSkillLine(*session, dbc, index)) {
    return luaL_error(L, "Bad skill line in ExpandTrainerSkillLine");
  }
  return 0;
}

int LuaGetSelectedSkill(lua_State *L) {
  lua_pushnumber(L, GetSelectedProfessionSkillIndex());
  return 1;
}

int LuaSetSelectedSkill(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetSelectedSkill(index)");
  }

  const auto skill_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
  openwow::game::SkillInfoStore::Get().SetSelectedSkillEntryIndex(skill_index);
  return 0;
}

}

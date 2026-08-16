#include "openwow/ui/game/framescript/widgets/frame_tooltip_methods.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/gossip_manager.h"
#include "openwow/game/trainer_frame.h"
#include "openwow/ui/game/tooltip_lua_adapter.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_quest.h"
#include "openwow/ui/game/tooltip_frame_sync.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
namespace {

using detail::GetDbcLoader;
using detail::GetLuaWidgetShownState;
using detail::GetWorldSession;
using detail::ResolveSpellBookSpellId;
using detail::SafeLuaString;
using detail::TruncateLuaNumberToSseI32;

constexpr std::uint32_t kTrainerTooltipSpellAttributeCreatesItem = 0x20u;
constexpr std::uint32_t kTrainerTooltipEffectCreateItem = 24u;
constexpr std::uint32_t kTrainerTooltipEffectLearnSpell = 36u;
constexpr std::uint32_t kTrainerTooltipEffectProxySpell = 57u;
constexpr std::uint32_t kTrainerTooltipEffectCreateItem2 = 59u;
constexpr std::uint32_t kTrainerTooltipEffectCreateItem3 = 157u;

int RaiseGameTooltipSetSpellSlotError(lua_State *L, int tooltip_index) {
  const char *name = openwow::ui::BorrowRawLuaStringField(L, tooltip_index, "__ow_name");
  return luaL_error(L, "Invalid spell slot in %s:SetSpell",
                    name != nullptr && *name != '\0' ? name : "<unnamed>");
}

std::optional<std::uint32_t> ResolveGameTooltipSpellbookSpellId(lua_State *L, int tooltip_index) {
  if (lua_isnumber(L, 2) == 0 || lua_isstring(L, 3) == 0) {
    RaiseGameTooltipSetSpellSlotError(L, tooltip_index);
    return std::nullopt;
  }

  const auto zero_based_slot = static_cast<std::int32_t>(lua_tonumber(L, 2) - 1.0);
  if (zero_based_slot < 0 || zero_based_slot >= 0x400) {
    RaiseGameTooltipSetSpellSlotError(L, tooltip_index);
    return std::nullopt;
  }

  return ResolveSpellBookSpellId(L, static_cast<std::uint32_t>(zero_based_slot + 1),
                                 SafeLuaString(L, 3));
}

bool IsTrainerTooltipItemEffect(const std::uint32_t effect_id) {
  return effect_id == kTrainerTooltipEffectCreateItem ||
         effect_id == kTrainerTooltipEffectCreateItem2 ||
         effect_id == kTrainerTooltipEffectCreateItem3;
}

const openwow::data::dbc::SpellEntry *
LookupTrainerTooltipSpellEntry(const openwow::data::dbc::DbcLoader &dbc,
                               const std::int32_t spell_id) {
  if (spell_id <= 0) {
    return nullptr;
  }
  return dbc.spell().LookupEntry(static_cast<std::uint32_t>(spell_id));
}

std::optional<std::uint32_t>
ResolveTrainerTooltipCreatedItemId(const openwow::data::dbc::SpellEntry &spell) {
  for (std::size_t index = 0; index < spell.effect.size(); ++index) {
    if (IsTrainerTooltipItemEffect(spell.effect[index]) && spell.effect_item_type[index] != 0) {
      return spell.effect_item_type[index];
    }
  }
  return std::nullopt;
}

const openwow::data::dbc::SpellEntry *
ResolveTrainerTooltipDisplaySpell(const openwow::data::dbc::DbcLoader &dbc,
                                  const openwow::data::dbc::SpellEntry &spell) {
  for (std::size_t index = 0; index < spell.effect.size(); ++index) {
    const std::uint32_t effect_id = spell.effect[index];
    if (effect_id != kTrainerTooltipEffectLearnSpell &&
        effect_id != kTrainerTooltipEffectProxySpell) {
      continue;
    }

    const std::uint32_t trigger_spell_id = spell.effect_trigger_spell[index];
    if (trigger_spell_id == 0) {
      continue;
    }

    if (const auto *trigger_spell = dbc.spell().LookupEntry(trigger_spell_id);
        trigger_spell != nullptr) {
      return trigger_spell;
    }
  }
  return nullptr;
}

std::optional<openwow::game::TrainerServiceTooltipTarget>
ResolveTrainerTooltipTargetFromTrainerSpell(const openwow::data::dbc::DbcLoader &dbc,
                                            const openwow::game::TrainerSpell &trainer_spell) {
  const auto *spell = LookupTrainerTooltipSpellEntry(dbc, trainer_spell.spell_id);
  if (spell == nullptr) {
    return std::nullopt;
  }

  const auto *display_spell = ResolveTrainerTooltipDisplaySpell(dbc, *spell);
  if (display_spell == nullptr) {
    display_spell = spell;
  }

  if ((display_spell->attributes & kTrainerTooltipSpellAttributeCreatesItem) != 0) {
    if (const auto item_id = ResolveTrainerTooltipCreatedItemId(*display_spell);
        item_id.has_value()) {
      return openwow::game::TrainerServiceTooltipTarget{
          openwow::game::TrainerServiceTooltipTargetKind::Item, *item_id};
    }
  }

  return openwow::game::TrainerServiceTooltipTarget{
      openwow::game::TrainerServiceTooltipTargetKind::Spell, display_spell->id};
}

std::optional<openwow::game::TrainerServiceTooltipTarget>
ResolveTrainerTooltipTargetFromPacketOrder(const openwow::game::WorldSession &session,
                                           const openwow::data::dbc::DbcLoader &dbc,
                                           const std::uint32_t one_based_index) {
  if (one_based_index == 0 || !session.gossip().has_trainer()) {
    return std::nullopt;
  }

  const auto &spells = session.gossip().trainer().spells;
  const std::size_t spell_index = static_cast<std::size_t>(one_based_index - 1u);
  if (spell_index >= spells.size()) {
    return std::nullopt;
  }

  return ResolveTrainerTooltipTargetFromTrainerSpell(dbc, spells[spell_index]);
}

}

void ApplyGameTooltipContentMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int tooltip_index = ValidateTypedFramescriptSelf(Ls, "GameTooltip");
    const auto spell_id = ResolveGameTooltipSpellbookSpellId(Ls, tooltip_index);
    if (!spell_id.has_value()) {
      return 0;
    }

    if (*spell_id == 0) {
      lua_pushnil(Ls);
      return 1;
    }

    auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
    const bool is_pet = openwow::text::EqualsIgnoreCaseAscii(
        SafeLuaString(Ls, 3), "pet");
    if (!tooltip_system.SetSpellById(*spell_id, true, is_pet)) {
      lua_pushnil(Ls);
      return 1;
    }
    SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
    lua_pushnumber(Ls, 1.0);
    return 1;
  });
  lua_setfield(L, f, "SetSpell");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipTotem.handler>(
      L, f, "SetTotem");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int tooltip_index = ValidateTypedFramescriptSelf(Ls, "GameTooltip");
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Invalid trainer service in SetTrainerService(index)");
    }

    const auto one_based_index =
        static_cast<std::uint32_t>(std::max(0, TruncateLuaNumberToSseI32(lua_tonumber(Ls, 2))));
    auto *session = GetWorldSession(Ls);
    const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
    if (dbc == nullptr) {
      dbc = GetDbcLoader(Ls);
    }

    if (session == nullptr || dbc == nullptr || one_based_index == 0) {
      return luaL_error(Ls, "Invalid trainer service in SetTrainerService(index)");
    }

    auto target =
        openwow::game::Trainer_ResolveVisibleServiceTooltipTarget(*session, dbc, one_based_index);
    if (!target.has_value()) {
      target = ResolveTrainerTooltipTargetFromPacketOrder(*session, *dbc, one_based_index);
    }
    if (!target.has_value() || target->id == 0) {
      return luaL_error(Ls, "Invalid trainer service in SetTrainerService(index)");
    }

    auto &tooltip_system = openwow::ui::game::TooltipSystem::Get();
    if (target->kind == openwow::game::TrainerServiceTooltipTargetKind::Item) {
      tooltip_system.SetItemById(target->id);
    } else {
      tooltip_system.SetSpellById(target->id);
    }
    SyncTooltipRegisteredLinesFromSystem(Ls, tooltip_index);
    return 0;
  });
  lua_setfield(L, f, "SetTrainerService");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipPetAction.handler>(
      L, f, "SetPetAction");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipQuestItem.trampoline>(
      L, f, "SetQuestItem");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipQuestLogItem.handler>(
      L, f, "SetQuestLogItem");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    ValidateTypedFramescriptSelf(Ls, "GameTooltip");
    const int ret =
        openwow::ui::game::detail::LuaCGTooltip_SetQuestLogRewardSpell(Ls);
    if (openwow::ui::game::TooltipSystem::Get().GetNumLines() > 0) {
      SyncTooltipRegisteredLinesFromSystem(Ls, lua_absindex(Ls, 1));
    }
    return ret;
  });
  lua_setfield(L, f, "SetQuestLogRewardSpell");

  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipQuestRewardSpell.trampoline>(
      L, f, "SetQuestRewardSpell");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipTradeSkillItem.trampoline>(
      L, f, "SetTradeSkillItem");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipTradePlayerItem.trampoline>(
      L, f, "SetTradePlayerItem");
  openwow::ui::game::frame_api::RegisterTooltipContentSetter<detail::kSetTooltipTradeTargetItem.trampoline>(
      L, f, "SetTradeTargetItem");
  lua_pushcfunction(L, detail::kAddTooltipTexture.trampoline);
  lua_setfield(L, f, "AddTexture");

  lua_pushcfunction(L, detail::kShowTooltip.trampoline);
  lua_setfield(L, f, "Show");

  lua_pushcfunction(L, detail::kHideTooltip.trampoline);
  lua_setfield(L, f, "Hide");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int tooltip_index = ValidateTypedFramescriptSelf(Ls, "GameTooltip");
    lua_pushboolean(Ls, GetLuaWidgetShownState(Ls, tooltip_index) ? 1 : 0);
    return 1;
  });
  lua_setfield(L, f, "IsShown");

  lua_pushcfunction(L, detail::kAppendTooltipText.trampoline);
  lua_setfield(L, f, "AppendText");

}

}

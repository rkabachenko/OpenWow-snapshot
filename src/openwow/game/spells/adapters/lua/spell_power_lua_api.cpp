#include "openwow/game/spells/adapters/lua/spell_power_lua_api.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/is_selected_spell.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/localization.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

#include <cmath>

namespace openwow::ui::game::detail {

namespace item_targeting = ::openwow::game::inventory::ui;

static const openwow::data::dbc::SpellEntry *LookupSpellDbc(lua_State *L, int spell_id) {
  return spell_id > 0 ? LookupSpellEntry(L, static_cast<std::uint32_t>(spell_id)) : nullptr;
}

static const openwow::game::CGUnit_C *
ResolveScriptSpellCaster(const openwow::game::WorldSession &session, bool from_pet_book) {
  if (!from_pet_book) {
    return session.objects().GetActivePlayer();
  }

  const auto &pet_bar = session.pet().pet_bar();
  if (!pet_bar.active || pet_bar.guid.IsEmpty()) {
    return nullptr;
  }

  return session.objects().GetUnit(pet_bar.guid);
}

static bool HasMeaningfulScriptRangeBounds(float min_range, float max_range) {

  constexpr float kRetailRangeEpsilon = 2.3841858e-7f;
  return std::isnan(min_range) || std::fabs(min_range) >= kRetailRangeEpsilon ||
         std::fabs(max_range) >= kRetailRangeEpsilon;
}

static openwow::game::UsabilityResult
BuildScriptSpellUsabilityResult(const openwow::game::SpellUsabilityInfo &spell_info,
                                const openwow::game::PlayerStateSnapshot &snapshot) {
  openwow::game::UsabilityResult result;
  result.power_type = spell_info.power_type;
  result.power_cost = openwow::game::SpellUsabilityChecker::ComputePowerCost(spell_info, snapshot);

  if (!openwow::game::SpellUsabilityChecker::CheckCasterRequirements(spell_info, snapshot)) {
    result.is_usable = false;
    return result;
  }

  if (!openwow::game::SpellUsabilityChecker::CheckPower(spell_info, snapshot)) {
    result.is_usable = false;
    result.not_enough_power = true;
    result.reason = openwow::game::UsabilityReason::kNotEnoughPower;
    return result;
  }

  result.is_usable = true;
  result.reason = openwow::game::UsabilityReason::kUsable;
  return result;
}

static std::optional<openwow::game::UsabilityResult>
ResolveScriptSpellUsability(const openwow::game::WorldSession &session,
                            const ScriptResolvedCurrentSpellQuery &query) {
  if (query.spell_id == 0) {
    return std::nullopt;
  }

  const auto spell = openwow::game::SpellQueryBridge::Get().Query(query.spell_id);
  if (!spell.has_value()) {
    return std::nullopt;
  }

  const auto *caster = ResolveScriptSpellCaster(session, query.from_pet_book);
  if (caster == nullptr) {
    return std::nullopt;
  }

  const auto *const player = session.objects().GetActivePlayer();
  const auto spell_info = openwow::game::BuildActionSpellUsabilityInfo(
      *spell, query.from_pet_book ? nullptr : player, player != nullptr ? &session.aura() : nullptr,
      session.GetDbcLoader());
  if (query.from_pet_book) {
    return BuildScriptSpellUsabilityResult(spell_info,
                                           openwow::game::BuildUnitUsabilitySnapshot(*caster));
  }

  if (player == nullptr) {
    return std::nullopt;
  }

  return BuildScriptSpellUsabilityResult(
      spell_info,
      openwow::game::BuildPlayerUsabilitySnapshot(
          *player, session.inventory_replica(), session.item_definitions(),
          &session.runes()));
}

int LuaGetSpellName(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "GetSpellName");
  if (!query.has_value()) {
    return 0;
  }

  const auto *spell = LookupSpellDbc(L, static_cast<int>(query->spell_id));
  if (spell) {
    lua_pushstring(L, std::string(spell->spell_name).c_str());
    lua_pushstring(L, std::string(spell->rank).c_str());
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
  }
  return 2;
}

int LuaGetSpellLink(lua_State *L) {
  const auto spell_id = ResolveSpellIdOrCurrentSpellQuery(L, "GetSpellLink");
  if (!spell_id.has_value() || *spell_id == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return spell_id.has_value() ? 2 : 0;
  }

  std::string name;
  const auto *spell = LookupSpellDbc(L, static_cast<int>(*spell_id));
  if (spell && !spell->spell_name.empty()) {
    name = std::string(spell->spell_name);
  } else {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  char buf[512];
  std::snprintf(buf, sizeof(buf), "|cff71d5ff|Hspell:%u|h[%s]|h|r", *spell_id, name.c_str());
  lua_pushstring(L, buf);
  const auto trade_link = BuildTradeSkillListLink(L, *spell_id);
  if (trade_link.has_value()) {
    lua_pushlstring(L, trade_link->data(), trade_link->size());
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaGetSpellTexture(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "GetSpellTexture");
  if (!query.has_value()) {
    return 0;
  }

  const auto texture =
      ::openwow::ui::game::detail::cursor_texture::ResolveSpellTexturePath(L, query->spell_id);
  if (texture.empty()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, texture.c_str());
  return 1;
}

int LuaGetSpellCritChanceFromIntellect(lua_State *L) {
  const LuaCallFrame call{L};
  const auto token = call.require_string(1, "Usage: GetSpellCritChanceFromIntellect(\"unit\")");
  return call.number(LuaDerivedStatQuery(call.state(), token).spell_crit_from_intellect());
}

int LuaIsUsableSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsUsableSpell");
  if (!query.has_value()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto usability = ResolveScriptSpellUsability(*session, *query);
  if (!usability.has_value() || !usability->is_usable) {
    lua_pushnil(L);
    if (usability.has_value() && usability->not_enough_power) {
      lua_pushwowbool(L, true);
    } else {
      lua_pushnil(L);
    }
    return 2;
  }

  lua_pushwowbool(L, true);
  lua_pushnil(L);
  return 2;
}

int LuaIsPassiveSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsPassiveSpell");
  if (!query.has_value()) {
    return 0;
  }

  const auto *spell = LookupSpellEntry(L, query->spell_id);
  lua_pushwowbool(L, spell && (spell->attributes & 0x40u) != 0);
  return 1;
}

int LuaIsHarmfulSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsHarmfulSpell");
  if (!query.has_value()) {
    return 0;
  }

  const auto *spell = LookupSpellEntry(L, query->spell_id);
  lua_pushwowbool(L, spell && ::openwow::game::GetHelpfulHarmfulDisposition(*spell) ==
                                  ::openwow::game::SpellHelpfulHarmfulDisposition::kHarmful);
  return 1;
}

int LuaIsHelpfulSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsHelpfulSpell");
  if (!query.has_value()) {
    return 0;
  }

  const auto *spell = LookupSpellEntry(L, query->spell_id);
  lua_pushwowbool(L, spell && ::openwow::game::GetHelpfulHarmfulDisposition(*spell) ==
                                  ::openwow::game::SpellHelpfulHarmfulDisposition::kHelpful);
  return 1;
}

int LuaIsConsumableSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsConsumableSpell");
  if (!query.has_value()) {
    return 0;
  }

  const auto *spell = LookupSpellEntry(L, query->spell_id);
  lua_pushwowbool(L, spell && SpellHasConsumableRequirement(*spell));
  return 1;
}

int LuaIsSpellInRange(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsSpellInRange");
  if (!query.has_value()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (!session || !dbc) {
    lua_pushnil(L);
    return 1;
  }

  const auto *caster = ResolveScriptSpellCaster(*session, query->from_pet_book);
  if (!caster) {
    lua_pushnil(L);
    return 1;
  }

  const auto unit_id = SafeLuaString(L, query->trailing_argument_index);
  const auto *target_object = unit_id.empty() ? nullptr : ResolveUnit(session, unit_id);
  const auto *target = target_object && target_object->IsUnit()
                           ? session->objects().GetUnit(target_object->GetGuid())
                           : nullptr;
  if (!target) {
    lua_pushnil(L);
    return 1;
  }

  const auto *spell = dbc->spell().LookupEntry(query->spell_id);
  const auto *range_entry =
      spell != nullptr && spell->range_index != 0
          ? dbc->spell_range().LookupEntry(spell->range_index)
          : nullptr;
  if (spell == nullptr || range_entry == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const bool use_friendly_range =
      caster->Interaction().CanAssistSpellTarget(*target, false);
  const auto range_window =
      ::openwow::game::SpellTargetValidator::GetTargetRangeWindow(
          *spell, range_entry, *caster, *target, use_friendly_range, session);
  if (range_window.min_range == 0.0f && range_window.max_range == 0.0f) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushnumber(L, ::openwow::game::SpellTargetValidator::IsTargetInRange(
                        *caster, *target, range_window)
                        ? 1.0
                        : 0.0);
  return 1;
}

int LuaSpellHasRange(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "SpellHasRange");
  if (!query.has_value()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto *dbc = GetDbcLoader(L);
  if (!dbc || !session) {
    lua_pushnil(L);
    return 1;
  }

  const auto *caster = ResolveScriptSpellCaster(*session, query->from_pet_book);
  const auto *spell = dbc->spell().LookupEntry(query->spell_id);
  if (!caster || !spell || spell->range_index == 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto *range_entry = dbc->spell_range().LookupEntry(spell->range_index);
  const bool use_friendly_range = ::openwow::game::GetHelpfulHarmfulDisposition(*spell) ==
                                  ::openwow::game::SpellHelpfulHarmfulDisposition::kHelpful;
  const auto window = ::openwow::game::SpellTargetValidator::GetUntargetedRangeWindow(
      *spell, range_entry, *caster, use_friendly_range, session);
  if (HasMeaningfulScriptRangeBounds(window.min_range, window.max_range)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetSpellAutocast(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "GetSpellAutocast");
  if (!query.has_value()) {
    return 0;
  }

  if (!query->from_pet_book || query->spellbook_slot == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto *spell =
      session->pet().GetSpellbookSpellEntry(static_cast<std::size_t>(query->spellbook_slot));
  if (!spell) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const bool allowed = spell->IsAutocastAllowed();
  const bool enabled = spell->IsAutocastEnabled();

  if (allowed)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);
  if (enabled)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);
  return 2;
}

int LuaEnableSpellAutocast(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "EnableSpellAutocast");
  if (!query.has_value() || !query->from_pet_book || query->spell_id == 0) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  ApplyPetSpellAutocastMutation(*session, query->spell_id, true);
  return 0;
}

int LuaDisableSpellAutocast(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "DisableSpellAutocast");
  if (!query.has_value() || !query->from_pet_book || query->spell_id == 0) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  ApplyPetSpellAutocastMutation(*session, query->spell_id, false);
  return 0;
}

openwow::ui::lua::NativeBindingCatalog SpellPowerConstantCatalog() {
  constexpr openwow::ui::LuaIntegerGlobal kSpellPowerConstants[] = {
      {"SPELL_POWER_MANA", 0},
      {"SPELL_POWER_RAGE", 1},
      {"SPELL_POWER_FOCUS", 2},
      {"SPELL_POWER_ENERGY", 3},
      {"SPELL_POWER_HAPPINESS", 4},
      {"SPELL_POWER_RUNES", 5},
      {"SPELL_POWER_RUNIC_POWER", 6},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.spells.power", openwow::ui::lua::BindingScope::kWorld,
      kSpellPowerConstants);
}

int LuaIsSelectedSpell(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "IsSelectedSpell");
  if (!query.has_value()) {
    return 0;
  }

  const auto* session = GetWorldSession(L);
  const auto* player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  const auto* dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(query->spell_id) : nullptr;

  std::uint32_t spell_skill_line = 0;
  if (player != nullptr && dbc != nullptr) {
    if (const auto resolved = openwow::game::ResolveSpellSkillLineId(
            session->objects(),
            dbc,
            player->State().GetRace(),
            player->State().GetClass(),
            query->spell_id)) {
      spell_skill_line = *resolved;
    }
  }

  auto& professions = openwow::game::ProfessionSystem::Get();
  const bool selected = openwow::game::IsSelectedSpellImpl(
      spell,
      professions.GetOpenSkillLine(),
      professions.IsTradeSkillLinked(),
      spell_skill_line,
      player != nullptr ? player->Animation().GetShapeshiftForm() : 0);
  if (selected) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 1;
}

int LuaSpellTargetItem(lua_State *L) {

  if (!GameUI_CanPerformProtectedAction(openwow::ui::game::protected_action_kind::kSpellCast))
    return 0;

  auto *session = GetWorldSession(L);
  if (!session || session->objects().GetActivePlayer() == nullptr)
    return 0;

  const auto &targeting = session->spells().GetTargeting();
  if (targeting.GetSpellId() == 0)
    return 0;

  const auto item_target_flags = ::openwow::game::SpellTargetFlag::kItem |
                                 ::openwow::game::SpellTargetFlag::kGoItem;
  if (!::openwow::game::HasFlag(
          static_cast<::openwow::game::SpellTargetFlag>(targeting.GetTargetMask()),
          item_target_flags))
    return 0;

  const ::openwow::game::ItemInstance *found_item = nullptr;

  if (lua_isnumber(L, 1) != 0) {
    const auto item_entry = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
    found_item = ::openwow::game::FindFirstDefaultCarriedInventoryItem(
        session->inventory_replica(),
        [item_entry](const ::openwow::game::ItemInstance &item) {
          return item.entry == item_entry;
        });
  } else {
    if (lua_isstring(L, 1) == 0) {
      luaL_error(L, "Usage: SpellTargetItem(itemID|\"name\"|\"itemlink\")");
      return 0;
    }
    const auto query = SafeLuaString(L, 1);
    if (query.empty())
      return 0;

    const auto parsed_link = ::openwow::game::ItemLinkParser::Parse(query);
    if (parsed_link.has_value()) {

      const auto link_entry = parsed_link->itemId;
      found_item = ::openwow::game::FindFirstDefaultCarriedInventoryItem(
          session->inventory_replica(),
          [link_entry](const ::openwow::game::ItemInstance &item) {
            return item.entry == link_entry;
          });
    } else {

      found_item = ::openwow::game::FindFirstDefaultCarriedInventoryItem(
          session->inventory_replica(),
          [&](const ::openwow::game::ItemInstance &item) {
            const auto *tmpl = session->query_cache().GetItemTemplate(item.entry);
            if (tmpl == nullptr)
              return false;
            return tmpl->name == query;
          });
    }
  }

  if (found_item == nullptr || found_item->guid == 0)
    return 0;

  item_targeting::ProcessItemSpellTarget(
      session->GetDbcLoader(), session->inventory_replica(),
      session->query_cache(), session->item_interactions(), session->objects(),
      session->interaction(), session->spells(),
      ::openwow::game::Localization::Get(),
      ::openwow::ui::UIErrorManager::Get(),
      ::openwow::ui::game::ScriptEventDispatch::Get(),
      ::openwow::game::ObjectGuid(found_item->guid),
      item_targeting::ItemTargetConfirmation::kPrompt);
  return 0;
}

}

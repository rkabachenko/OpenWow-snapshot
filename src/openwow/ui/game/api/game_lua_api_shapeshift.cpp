
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_shapeshift.h"

#include "openwow/game/attack_action_shapeshift.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/ui/lua_numeric.h"

namespace openwow::ui::game::detail {

namespace {

constexpr std::uint8_t kCancelableAuraSlotFlag = 0x10u;

constexpr std::uint32_t kShapeshiftFormSuppressManualCancelFlag = 0x2u;

[[nodiscard]] bool HasRetailAuraApplyingEffect(
    const openwow::data::dbc::SpellEntry& spell) {

  for (const auto effect : spell.effect) {
    switch (effect) {
      case 6u:
      case 0x23u:
      case 0x41u:
      case 0x77u:
      case 0x80u:
      case 0x81u:
      case 0x8Fu:
        return true;
      default:
        break;
    }
  }
  return false;
}

}

int LuaGetNumShapeshiftForms(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
  lua_pushnumber(L, static_cast<lua_Number>(form_spells.size()));
  return 1;
}

int LuaGetShapeshiftFormInfo(lua_State* L) {
  int index = static_cast<int>(lua_tonumber(L, 1));
  if (index <= 0) {
    luaL_error(L, "Usage: GetShapeshiftFormInfo(index)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return 0;
  }

  const auto* dbc = session->GetDbcLoader();
  if (dbc == nullptr) {
    return 0;
  }

  const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
  const std::size_t zero_based_index = static_cast<std::size_t>(index) - 1;
  if (zero_based_index >= form_spells.size()) {
    return 0;
  }

  const auto spell_id = form_spells[zero_based_index];
  const auto* spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return 0;
  }

  bool pushed_icon = false;
  if (spell->spell_icon_id != 0) {
    if (const auto* icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
        icon != nullptr && !icon->icon_path.empty()) {
      lua_pushstring(L, std::string(icon->icon_path).c_str());
      pushed_icon = true;
    }
  }
  if (!pushed_icon) {
    const auto form_id = openwow::game::ResolveShapeshiftFormIdFromSpell(*session, spell_id);
    if (form_id != 0) {
      if (const auto* form = dbc->spell_shapeshift_form().LookupEntry(form_id);
          form != nullptr && form->attack_icon_id != 0) {
        if (const auto* icon =
                dbc->spell_icon().LookupEntry(form->attack_icon_id);
            icon != nullptr && !icon->icon_path.empty()) {
          lua_pushstring(L, std::string(icon->icon_path).c_str());
          pushed_icon = true;
        }
      }
    }
  }
  if (!pushed_icon) {
    lua_pushnil(L);
  }

  if (!spell->spell_name.empty()) {
    lua_pushstring(L, std::string(spell->spell_name).c_str());
  } else {
    lua_pushnil(L);
  }

  const auto active_form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
  const auto spell_form_id = openwow::game::ResolveShapeshiftFormIdFromSpell(*session, spell_id);
  if (active_form_id != 0 && spell_form_id == active_form_id) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  const std::uint32_t current_power = player->State().GetPower(static_cast<std::uint8_t>(spell->power_type));
  if (current_power >= spell->mana_cost) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }

  return 4;
}

int LuaGetShapeshiftForm(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const bool exclude_temporary = ScriptReadBoolArgOrDefault(L, 1, false);
  const auto active_form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
  if (exclude_temporary && active_form_id == 0) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
  const auto* dbc = session->GetDbcLoader();
  for (std::size_t i = form_spells.size(); i > 0; --i) {
    const auto* spell =
        dbc != nullptr ? dbc->spell().LookupEntry(form_spells[i - 1]) : nullptr;
    if (spell != nullptr &&
        openwow::game::IsSpellRecordCurrentForUnit(*spell, *dbc, player)) {
      lua_pushnumber(L, static_cast<lua_Number>(i - 1));
      return 1;
    }
  }

  lua_pushnumber(L, active_form_id != 0
                        ? static_cast<lua_Number>(form_spells.size() + 1)
                        : 0.0);
  return 1;
}

int LuaCastShapeshiftForm(lua_State* L) {
  const auto index = lua_tonumber(L, 1);
  if (index == 0.0) {
    luaL_error(L, "Usage: CastShapeshiftForm(index)");
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
  const auto zero_based_index = openwow::ui::SaturateLuaNumberToU32(index) - 1u;
  if (zero_based_index >= form_spells.size()) {
    return 0;
  }

  const auto spell_id = form_spells[zero_based_index];
  if (spell_id == 0) {
    return 0;
  }

  const auto* dbc = session->GetDbcLoader();
  const auto* player = session->objects().GetLocalPlayerTyped();
  const auto* spell =
      dbc != nullptr ? dbc->spell().LookupEntry(spell_id) : nullptr;
  if (spell == nullptr || player == nullptr) {
    return 0;
  }

  if (openwow::game::IsSpellRecordCurrentForUnit(*spell, *dbc, player)) {
    return LuaCancelShapeshiftForm(L);
  }

  (void)openwow::game::SpellAction_ValidateAndInitiateCast(
      *session, spell_id, 0u, -1, 0);
  return 0;
}

int LuaCancelShapeshiftForm([[maybe_unused]] lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) return 0;

  const auto* player = session->objects().GetLocalPlayerTyped();
  if (!player) return 0;

  if (player->State().SuppressesCurrentFormSpellQueries() ||
      !player->State().GetCharmedUnitGUID().IsEmpty()) {
    return 0;
  }

  const auto form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
  if (form_id == 0u) return 0;

  const auto* dbc = session->GetDbcLoader();
  if (!dbc) return 0;

  const auto* form = dbc->spell_shapeshift_form().LookupEntry(form_id);
  if (form == nullptr ||
      (form->flags & kShapeshiftFormSuppressManualCancelFlag) != 0u) {
    return 0;
  }

  for (const auto& aura : player->Auras().All()) {
    if (aura.spell_id == 0u) continue;
    if ((static_cast<std::uint8_t>(aura.flags) &
         kCancelableAuraSlotFlag) == 0u) {
      continue;
    }

    const auto* spell = dbc->spell().LookupEntry(aura.spell_id);
    if (!spell) continue;

    if (HasRetailAuraApplyingEffect(*spell) &&
        openwow::game::SpellAppliesShapeshiftForm(*spell, form_id)) {
      session->interaction().SendCancelAura(aura.spell_id);
    }
  }

  return 0;
}

}

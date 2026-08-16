#include "openwow/game/spells/spellbook/adapters/lua/spellbook_lua_api.h"
#include "openwow/game/shapeshift_form_resolver.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cstring>
#include <string_view>

namespace openwow::ui::game::detail {

int LuaHasPetSpells(lua_State* L) {
  auto* session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto spell_count = session->pet().GetSpellbookSpellCount();
  if (spell_count == 0) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  lua_pushnumber(L, static_cast<lua_Number>(spell_count));
  std::string_view pet_token = "PET";
  const auto* player = session->objects().GetActivePlayer();
  const auto* dbc = session->GetDbcLoader();
  if (player != nullptr && dbc != nullptr) {
    if (const auto* player_class =
            dbc->chr_classes().LookupEntry(player->State().GetClass());
        player_class != nullptr && !player_class->pet_name_token.empty()) {
      pet_token = player_class->pet_name_token;
    }
  }
  lua_pushlstring(L, pet_token.data(), pet_token.size());
  return 2;
}

int LuaGetKnownSlotFromHighestRankSlot(lua_State* L) {
  const auto requested_slot = lua_tointeger(L, 1);
  std::uint32_t known_slot = 0;
  if (requested_slot > 0 &&
      static_cast<std::uint64_t>(requested_slot) <=
          std::numeric_limits<std::uint32_t>::max()) {
    known_slot = openwow::game::SpellbookSystem::Get().GetKnownSlotFromHighestRankSlot(
        static_cast<std::uint32_t>(requested_slot));
  }

  FrameScript_PushNumberFromInt(L, static_cast<int>(known_slot));
  return 1;
}

openwow::ui::lua::NativeBindingCatalog SpellBookConstantCatalog() {
  constexpr openwow::ui::LuaStringGlobal kSpellBookConstants[] = {
      {"BOOKTYPE_SPELL", "spell"},
      {"BOOKTYPE_PET", "pet"},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.spells.spellbook", openwow::ui::lua::BindingScope::kWorld,
      kSpellBookConstants);
}

int LuaGetShapeshiftFormCooldown(lua_State* L) {
  if (!lua_isnumber(L, 1) || lua_tonumber(L, 1) == 0.0) {
    return luaL_error(L, "Usage: GetShapeshiftFormCooldown(index)");
  }

  const auto *session = GetWorldSession(L);
  if (session != nullptr) {
    const auto form_spells = openwow::game::ResolveShapeshiftFormSpellIds(session);
    const auto zero_based =
        openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1)) - 1u;
    if (zero_based < form_spells.size()) {
      const auto cooldown =
          openwow::game::ResolveSpellbookCooldown(session->spell_book(), form_spells[zero_based]);
      if (cooldown.has_value()) {
        lua_pushnumber(L, cooldown->start_time_s);
        lua_pushnumber(L, cooldown->duration_s);
        lua_pushnumber(L, cooldown->enabled);
        return 3;
      }
    }
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 1.0);
  return 3;
}

}

namespace openwow::game::spells::spellbook::adapters::lua {

std::optional<std::string> ResolveCastSequenceToken(
    lua_State* state, const std::string_view body) {
  if (state == nullptr) {
    return std::nullopt;
  }

  const int stack_top = lua_gettop(state);
  int errfunc = 0;

  lua_getfield(state, LUA_REGISTRYINDEX,
               openwow::ui::kGameLuaErrorHandlerRegistryKey);
  if (lua_isfunction(state, -1) || lua_iscfunction(state, -1)) {
    errfunc = lua_gettop(state);
  } else {
    lua_pop(state, 1);
  }

  lua_getglobal(state, "QueryCastSequence");
  if (!lua_isfunction(state, -1) && !lua_iscfunction(state, -1)) {
    lua_settop(state, stack_top);
    return std::nullopt;
  }
  lua_pushlstring(state, body.data(), body.size());
  if (lua_pcall(state, 1, 3, errfunc) != 0) {
    lua_settop(state, stack_top);
    return std::nullopt;
  }

  std::optional<std::string> resolved;
  if (const char* secondary = lua_tostring(state, -2);
      secondary != nullptr) {
    resolved = std::string(secondary);
  } else if (const char* tertiary = lua_tostring(state, -1);
             tertiary != nullptr) {
    resolved = std::string(tertiary);
  }

  lua_settop(state, stack_top);
  return resolved;
}

}

namespace openwow::ui::game::detail {

int LuaToggleSpellAutocast(lua_State* L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "ToggleSpellAutocast");
  if (!query.has_value() || !query->from_pet_book || query->spell_id == 0) {
    return 0;
  }

  auto* session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  ApplyPetSpellAutocastMutation(*session, query->spell_id, std::nullopt);
  return 0;
}

int LuaFindSpellBookSlotByID(lua_State* L) {
  if (!lua_isnumber(L, 1) || static_cast<int>(lua_tonumber(L, 1)) <= 0)
    luaL_error(L, "Usage: FindSpellBookSlotByID(spellID[, isPet])");
  const auto spell_id = static_cast<std::uint32_t>(lua_tonumber(L, 1));
  const bool is_pet_book = ScriptReadBoolArgOrDefault(L, 2, false);

  const auto slot_index =
      FindSpellBookSlotIndexBySpellId(L, spell_id, is_pet_book);
  if (!slot_index.has_value()) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushinteger(L, static_cast<lua_Integer>(*slot_index));
  return 1;
}

}

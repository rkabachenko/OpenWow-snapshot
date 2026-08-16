#include "openwow/ui/game/api/held_cursor_lua_api.h"

#include "openwow/game/commerce/adapters/ui/money_cursor_controller.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/lua_numeric.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {
namespace cursor = ::openwow::game::actions::held_cursor;

int LuaCursorHasItem(lua_State* state) {
  const auto* held = lua::FindHeldCursor(*state);
  lua_pushwowbool(
      state, held != nullptr &&
                 (held->kind() == cursor::Kind::LiveItem ||
                  held->kind() == cursor::Kind::AmmoItem));
  return 1;
}

int LuaCursorHasMacro(lua_State* state) {
  const auto* held = lua::FindHeldCursor(*state);
  lua_pushwowbool(
      state, held != nullptr && held->kind() == cursor::Kind::Macro);
  return 1;
}

int LuaCursorHasMoney(lua_State* state) {
  const auto* held = lua::FindHeldCursor(*state);
  lua_pushwowbool(
      state, held != nullptr && held->kind() == cursor::Kind::PlayerMoney);
  return 1;
}

int LuaCursorHasSpell(lua_State* state) {
  const auto* held = lua::FindHeldCursor(*state);
  lua_pushwowbool(
      state, held != nullptr && held->kind() == cursor::Kind::Spell);
  return 1;
}

int LuaDropCursorMoney(lua_State* state) {
  auto* held = lua::FindHeldCursor(*state);
  if (held != nullptr && held->kind() == cursor::Kind::PlayerMoney) {
    held->Clear();
  }
  return 0;
}

int LuaPickupPlayerMoney(lua_State* state) {
  if (!lua_isnumber(state, 1)) {
    return luaL_error(state, "Usage: PickupPlayerMoney(amount)");
  }
  auto* session = GetWorldSession(state);
  const auto* player =
      session != nullptr ? session->objects().GetLocalPlayerTyped() : nullptr;
  const auto amount = SaturateLuaNumberToU32(lua_tonumber(state, 1));
  auto* held = lua::FindHeldCursor(*state);
  if (player == nullptr || held == nullptr || amount == 0 ||
      amount > player->GetMoney()) {
    return 0;
  }
  held->Clear();
  const auto effect = ::openwow::game::commerce::ui::PickupMoneyCursor(
      *held, ::openwow::game::commerce::CopperAmount(amount),
      ::openwow::game::commerce::ui::MoneyCursorKind::kPlayerMoney);
  if (effect == ::openwow::game::commerce::ui::
                    MoneyCursorPickupEffect::kCursorUpdate) {
    ::openwow::ui::game::ScriptEventDispatch::Get().FirePlayerMoney();
  }
  return 0;
}

}

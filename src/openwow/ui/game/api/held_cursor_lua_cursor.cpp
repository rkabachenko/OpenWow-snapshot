#include "openwow/ui/game/api/held_cursor_lua_api.h"

#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/merchant_repair_cost.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"

#include <lua.hpp>

#include <cstdint>

namespace openwow::ui::game::detail {
namespace {

constexpr std::uint32_t kRetailDefaultCursorType = 1;
constexpr std::uint32_t kRetailRepairCursorType = 17;
bool repair_mode = false;

openwow::game::CursorSurface* CursorManager(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, "openwow.cursor_manager");
  auto* manager = static_cast<openwow::game::CursorSurface*>(
      lua_touserdata(state, -1));
  lua_pop(state, 1);
  return manager;
}

bool HasRepairBaseCursor(const openwow::game::CursorSurface* manager) {
  return manager != nullptr &&
         manager->GetBaseRetailCursorType() == kRetailRepairCursorType;
}

bool IsRepairMode(lua_State* state) {
  if (auto* manager = CursorManager(state); manager != nullptr) {
    return HasRepairBaseCursor(manager);
  }
  return repair_mode;
}

void SetRepairMode(lua_State* state, const bool enabled) {
  repair_mode = enabled;
  if (auto* manager = CursorManager(state); manager != nullptr) {
    manager->SetBaseCursor(enabled ? openwow::game::CursorType::kRepair
                                   : openwow::game::CursorType::kDefault);
    manager->SetImmediateCursorType(
        enabled ? static_cast<int>(kRetailRepairCursorType)
                : static_cast<int>(kRetailDefaultCursorType));
  }
}

}

int LuaShowCursor(lua_State* state) {
  if (auto* manager = CursorManager(state); manager != nullptr) {
    manager->ShowCursor(true);
  }
  return 0;
}

int LuaHideCursor(lua_State* state) {
  if (auto* manager = CursorManager(state); manager != nullptr) {
    manager->ShowCursor(false);
  }
  return 0;
}

bool IsRepairCursorModeActive() {
  return HasRepairBaseCursor(openwow::game::GetActiveCursorSurface()) ||
         repair_mode;
}

int LuaInRepairMode(lua_State* state) {
  if (IsRepairMode(state)) {
    lua_pushnumber(state, 1);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaShowRepairCursor(lua_State* state) {
  auto* session = GetWorldSession(state);
  if (session == nullptr ||
      session->spells().GetTargeting().GetSpellId() != 0 ||
      ResolveMerchantRepairVendor(session->gossip(), session->objects()) ==
          nullptr) {
    return 0;
  }
  if (auto* cursor = lua::FindHeldCursor(*state); cursor != nullptr) {
    cursor->Clear();
  }
  SetRepairMode(state, true);
  return 0;
}

int LuaHideRepairCursor(lua_State* state) {
  if (IsRepairMode(state)) SetRepairMode(state, false);
  return 0;
}

}

#include "openwow/game/inventory/items/adapters/lua/item_lua_api.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/game/inventory/items/item_interactions.h"
#include "openwow/game/object_guid.h"

#include <lua.hpp>

namespace openwow::ui::game::detail {
namespace {

openwow::game::ObjectGuid ReadableItem(const ItemLuaAdapter& adapter) {
  const auto& readable = adapter.item_interactions().readable();
  return readable.has_value() ? readable->item : openwow::game::ObjectGuid{};
}

}

int LuaItemTextGetCreator(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto value = adapter.ReadableCreator(ReadableItem(adapter));
  if (!value.has_value()) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, value->data(), value->size());
  }
  return 1;
}

int LuaItemTextGetItem(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto value = adapter.ReadableName(state, ReadableItem(adapter));
  if (!value.has_value()) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, value->data(), value->size());
  }
  return 1;
}

int LuaItemTextGetMaterial(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto value = adapter.ReadableMaterial(state, ReadableItem(adapter));
  if (!value.has_value()) {
    lua_pushnil(state);
  } else {
    lua_pushlstring(state, value->data(), value->size());
  }
  return 1;
}

int LuaItemTextGetPage(lua_State* state) {
  const auto& readable =
      RequireItemLuaAdapter(state).item_interactions().readable();
  lua_pushnumber(
      state, static_cast<lua_Number>(
                 readable.has_value() ? readable->page + 1 : 1));
  return 1;
}

int LuaItemTextGetText(lua_State* state) {
  const auto& readable =
      RequireItemLuaAdapter(state).item_interactions().readable();
  if (!readable.has_value()) {
    lua_pushliteral(state, "");
  } else {
    lua_pushlstring(state, readable->text.data(), readable->text.size());
  }
  return 1;
}

int LuaItemTextHasNextPage(lua_State* state) {
  const auto& readable =
      RequireItemLuaAdapter(state).item_interactions().readable();
  if (readable.has_value() && readable->page + 1 < readable->pages.size() &&
      readable->pages[readable->page + 1] != 0) {
    lua_pushnumber(state, 1);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaItemTextNextPage(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  auto& readable = adapter.item_interactions().readable();
  if (readable.has_value() && readable->page + 1 < readable->pages.size() &&
      readable->pages[readable->page + 1] != 0) {
    ++readable->page;
    adapter.LoadReadablePage(true);
  }
  return 0;
}

int LuaItemTextPrevPage(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  auto& readable = adapter.item_interactions().readable();
  if (readable.has_value() && readable->page > 0 &&
      !readable->pages.empty() && readable->pages[0] != 0) {
    --readable->page;
    adapter.LoadReadablePage(true);
  }
  return 0;
}

int LuaCloseItemText(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const bool was_open = adapter.item_interactions().readable().has_value();
  adapter.item_interactions().close_readable();
  if (was_open) {
    adapter.PresentReadableClosed();
  }
  return 0;
}

}

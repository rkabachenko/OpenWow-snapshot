#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/script_object_lookup.h"

#include "openwow/ui/game/script_event_dispatch.h"

#include <lua.hpp>

#include <cstring>

namespace openwow::ui {

namespace {

widgets::ScriptObjectType ResolveTableObjectType(lua_State* L, int index) noexcept {
  index = lua_absindex(L, index);
  lua_getfield(L, index, "__ow_type");
  const char* type_name = lua_tostring(L, -1);
  widgets::ScriptObjectType result = widgets::ScriptObjectType::COUNT_;

  if (type_name != nullptr && *type_name != '\0') {

    if (std::strcmp(type_name, "ModelFFX") == 0) {
      result = widgets::ScriptObjectType::Model;
    } else if (std::strcmp(type_name, "QuestPOIFrame") == 0) {
      result = widgets::ScriptObjectType::Frame;
    } else {
      result = widgets::ScriptObjectTypeFromName(type_name);
    }
  }

  lua_pop(L, 1);
  return result;
}

}

void* Script_FindNamedObjectByTypeTag(
    const char* name, widgets::ScriptObjectType requiredType) noexcept {
  lua_State* L = game::ScriptEventDispatch::Get().GetLuaState();
  if (L == nullptr || name == nullptr || *name == '\0') {
    return nullptr;
  }

  lua_getglobal(L, name);

  if (lua_type(L, -1) != LUA_TTABLE) {
    lua_pop(L, 1);
    return nullptr;
  }

  lua_rawgeti(L, -1, 0);
  void* this_ptr = lua_touserdata(L, -1);

  bool type_ok = false;
  if (this_ptr != nullptr) {
    const widgets::ScriptObjectType actual = ResolveTableObjectType(L, -2);
    type_ok = widgets::IsScriptTypeKindOf(actual, requiredType);
  }

  lua_pop(L, 2);

  return type_ok ? this_ptr : nullptr;
}

}

#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/widgets/script_object.h"

#include <lua.hpp>

namespace openwow::ui::game::frame_api {

int ValidateFrameScriptSelf(lua_State* lua) {
  if (lua_istable(lua, 1) == 0) {
    luaL_error(
        lua,
        "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  const auto canonical_type = lua_adapter::CanonicalScriptObjectType(lua, 1);
  if (!lua_adapter::HasScriptObjectIdentity(lua, 1) || canonical_type ==
          openwow::ui::widgets::ScriptObjectType::COUNT_) {
    luaL_error(lua, "Attempt to find 'this' in non-framescript object");
  }
  return lua_absindex(lua, 1);
}

int ValidateFrameObjectSelf(lua_State* lua, const char* expected_type) {
  using openwow::ui::widgets::ScriptObjectType;

  const int self = ValidateFrameScriptSelf(lua);
  const auto expected =
      openwow::ui::widgets::ScriptObjectTypeFromName(expected_type);
  if (expected == ScriptObjectType::COUNT_ ||
      !lua_adapter::IsScriptObjectKindOf(lua, self, expected)) {
    luaL_error(lua, "Wrong object type for member function");
  }
  return self;
}

}

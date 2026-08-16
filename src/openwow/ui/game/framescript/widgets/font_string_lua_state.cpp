#include "openwow/ui/game/framescript/widgets/font_string_state_methods.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include <lua.hpp>
namespace openwow::ui::game::frame_api {
namespace { constexpr const char* kFontStringIndentedWordWrapField = "__ow_indented_wrap"; }
using detail::ScriptReadBoolArgOrDefault;
using detail::lua_pushwowbool;
int PushTableJustify(lua_State *L, const char *field_name, const char *default_value,
                     bool horizontal) {
  if (lua_istable(L, 1) == 0) {
    return luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  lua_getfield(L, 1, field_name);
  const char *stored = lua_tostring(L, -1);
  if (stored == nullptr || *stored == '\0') {
    lua_pop(L, 1);
    lua_pushstring(L, default_value);
    return 1;
  }

  uint32_t flags = 0;
  const int parsed = horizontal ? openwow::ui::StringToHorizontalJustify(stored, &flags)
                                : openwow::ui::StringToVerticalJustify(stored, &flags);
  lua_pop(L, 1);
  lua_pushstring(L, horizontal ? openwow::ui::HorizontalJustifyFlagsToString(parsed ? flags : 0)
                               : openwow::ui::VerticalJustifyFlagsToString(parsed ? flags : 0));
  return 1;
}

void SetFontStringWrapState(lua_State *L, const int self_idx, const char *field_name,
                            const bool enabled) {
  lua_pushboolean(L, enabled ? 1 : 0);
  lua_setfield(L, self_idx, field_name);
  NotifyFrameInputMutation(L, self_idx, false);
}

bool GetFontStringWrapState(lua_State *L, const int self_idx, const char *field_name,
                            const bool default_value) {
  lua_getfield(L, self_idx, field_name);
  const bool enabled = lua_isnil(L, -1) ? default_value : (lua_toboolean(L, -1) != 0);
  lua_pop(L, 1);
  return enabled;
}

int SetFontStringIndentedWordWrap(lua_State *L) {
  const int self_idx = ValidateTypedFramescriptSelf(L, "FontString");
  SetFontStringWrapState(
      L, self_idx, kFontStringIndentedWordWrapField,
      ScriptReadBoolArgOrDefault(L, 2, true));
  return 0;
}

int GetFontStringIndentedWordWrap(lua_State *L) {
  const int self_idx = ValidateTypedFramescriptSelf(L, "FontString");
  lua_pushwowbool(L, GetFontStringWrapState(
                         L, self_idx, kFontStringIndentedWordWrapField, false));
  return 1;
}

}

#include <lua.hpp>

#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"
#include "openwow/ui/lua_c_api_convenience.h"
#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
using detail::lua_pushwowbool;
using detail::ScriptReadBoolArgOrDefault;
void ApplyScrollingMessageFrameStateMethods(lua_State* L) {
  const int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_smf_hyperlinks");
    const bool enabled =
        lua_isboolean(Ls, -1) == 0 || lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    lua_pushwowbool(Ls, enabled);
    return 1;
  });
  lua_setfield(L, f, "GetHyperlinksEnabled");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_smf_hyperlinks");
    }
    return 0;
  });
  lua_setfield(L, f, "SetHyperlinksEnabled");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:GetMessageInfo(index[, accessID])",
                        GetFrameUsageObjectName(Ls, self_idx).c_str());
    }
    lua_Integer idx =
        static_cast<lua_Integer>(std::max<lua_Number>(lua_tonumber(Ls, 2), 0));
    const lua_Integer access_id =
        lua_isnumber(Ls, 3) != 0 ? static_cast<lua_Integer>(lua_tonumber(Ls, 3))
                                 : 0;

    lua_getfield(Ls, 1, "__ow_smf_messages");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      return luaL_error(Ls,
                        "%s:GetMessageInfo - A line with accessID %d and index "
                        "%d does not exist.",
                        GetFrameUsageObjectName(Ls, self_idx).c_str(),
                        static_cast<int>(access_id), static_cast<int>(idx - 1));
    }

    lua_getfield(Ls, self_idx, "__ow_smf_num_msg");
    const lua_Integer count =
        lua_isnumber(Ls, -1) != 0 ? lua_tointeger(Ls, -1) : 0;
    lua_pop(Ls, 1);
    lua_Integer matching_index = 0;
    bool found = false;
    for (lua_Integer i = 1; i <= count; ++i) {
      lua_geti(Ls, -1, i);
      if (lua_istable(Ls, -1) != 0) {
        lua_getfield(Ls, -1, "accessID");
        const lua_Integer candidate =
            lua_isnumber(Ls, -1) != 0 ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        if ((access_id == 0 || candidate == access_id) &&
            ++matching_index == idx) {
          lua_remove(Ls, -2);
          found = true;
          break;
        }
      }
      lua_pop(Ls, 1);
    }

    if (!found) {
      lua_pop(Ls, 1);
      return luaL_error(Ls,
                        "%s:GetMessageInfo - A line with accessID %d and index "
                        "%d does not exist.",
                        GetFrameUsageObjectName(Ls, self_idx).c_str(),
                        static_cast<int>(access_id), static_cast<int>(idx - 1));
    }

    int msg = lua_absindex(Ls, -1);
    lua_getfield(Ls, msg, "text");
    lua_getfield(Ls, msg, "accessID");
    lua_getfield(Ls, msg, "lineID");
    lua_getfield(Ls, msg, "extraData");
    return 4;
  });
  lua_setfield(L, f, "GetMessageInfo");

  lua_pushcfunction(L, [](lua_State* Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:RemoveMessagesByAccessID(accessID)",
                        GetFrameUsageObjectName(Ls, self_idx).c_str());
    }
    lua_Integer targetID = static_cast<lua_Integer>(lua_tonumber(Ls, 2));

    lua_getfield(Ls, 1, "__ow_smf_messages");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      return 0;
    }
    int msgs = lua_absindex(Ls, -1);

    lua_getfield(Ls, 1, "__ow_smf_num_msg");
    lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
    lua_pop(Ls, 1);

    lua_newtable(Ls);
    int newMsgs = lua_absindex(Ls, -1);
    lua_Integer newCount = 0;

    for (lua_Integer i = 1; i <= n; ++i) {
      lua_geti(Ls, msgs, i);
      if (lua_istable(Ls, -1)) {
        lua_getfield(Ls, -1, "accessID");
        lua_Integer aid = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        if (aid != targetID) {
          newCount++;
          lua_seti(Ls, newMsgs, newCount);
        } else {
          lua_pop(Ls, 1);
        }
      } else {
        lua_pop(Ls, 1);
      }
    }

    lua_setfield(Ls, 1, "__ow_smf_messages");
    lua_pop(Ls, 1);

    lua_pushinteger(Ls, newCount);
    lua_setfield(Ls, 1, "__ow_smf_num_msg");
    return 0;
  });
  lua_setfield(L, f, "RemoveMessagesByAccessID");
}

}

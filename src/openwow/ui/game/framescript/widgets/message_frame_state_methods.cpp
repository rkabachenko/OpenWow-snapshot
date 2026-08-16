#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/widgets/simple_message_frame.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include <lua.hpp>
#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

#include <cstdint>
namespace openwow::ui::game::frame_api {
void ApplyMessageFrameStateMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 1);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_smf_fading");
    lua_pushboolean(Ls, lua_isnil(Ls, -1) ? 1 : lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  });
  lua_setfield(L, f, "GetFading");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 3);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_smf_fadedur");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 3);
    }
    return 1;
  });
  lua_setfield(L, f, "GetFadeDuration");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 10);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_smf_timevis");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 10);
    }
    return 1;
  });
  lua_setfield(L, f, "GetTimeVisible");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, "BOTTOM");
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_smf_insert");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "BOTTOM");
    }
    return 1;
  });
  lua_setfield(L, f, "GetInsertMode");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(
          Ls,
          "Usage: %s:AddMessage(\"text\", [r, g, b,] typeID, backFill, accessID, extraData)",
          GetFrameUsageObjectName(Ls, self_idx).c_str());
    }

    const char *text = lua_tostring(Ls, 2);
    if (text == nullptr || *text == '\0') {
      return 0;
    }

    const bool has_color = lua_isnumber(Ls, 3) != 0 && lua_isnumber(Ls, 4) != 0 &&
                           lua_isnumber(Ls, 5) != 0;
    const int metadata_arg = has_color ? 6 : 3;
    const auto quantize_color = [](const lua_Number value) {
      const double clamped = std::clamp(static_cast<double>(value), 0.0, 1.0);
      return std::floor(clamped * 255.0 + 0.5) / 255.0;
    };
    const auto default_color = [&](const char* field) {
      lua_getfield(Ls, self_idx, field);
      const double value =
          lua_isnumber(Ls, -1) != 0 ? lua_tonumber(Ls, -1) : 1.0;
      lua_pop(Ls, 1);
      return quantize_color(value);
    };
    const double r =
        has_color ? quantize_color(lua_tonumber(Ls, 3))
                  : default_color("__ow_text_r");
    const double g =
        has_color ? quantize_color(lua_tonumber(Ls, 4))
                  : default_color("__ow_text_g");
    const double b =
        has_color ? quantize_color(lua_tonumber(Ls, 5))
                  : default_color("__ow_text_b");
    const lua_Integer line_id = lua_isnumber(Ls, metadata_arg) != 0
                                    ? static_cast<lua_Integer>(lua_tonumber(Ls, metadata_arg))
                                    : 0;
    const bool back_fill = lua_type(Ls, metadata_arg + 1) == LUA_TBOOLEAN &&
                           lua_toboolean(Ls, metadata_arg + 1) != 0;
    const lua_Integer access_id = lua_isnumber(Ls, metadata_arg + 2) != 0
                                      ? static_cast<lua_Integer>(lua_tonumber(Ls, metadata_arg + 2))
                                      : 0;
    const lua_Integer extra_data = lua_isnumber(Ls, metadata_arg + 3) != 0
                                       ? static_cast<lua_Integer>(lua_tonumber(Ls, metadata_arg + 3))
                                       : 0;

    lua_getfield(Ls, 1, "__ow_smf_messages");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_newtable(Ls);
      lua_pushvalue(Ls, -1);
      lua_setfield(Ls, 1, "__ow_smf_messages");
    }
    int msgs = lua_absindex(Ls, -1);

    lua_getfield(Ls, 1, "__ow_smf_num_msg");
    lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_smf_maxlines");
    const lua_Integer max_lines =
        lua_isnumber(Ls, -1) != 0
            ? std::max<lua_Integer>(1, lua_tointeger(Ls, -1))
            : 128;
    lua_pop(Ls, 1);

    lua_newtable(Ls);
    int msg = lua_absindex(Ls, -1);
    lua_pushstring(Ls, text);
    lua_setfield(Ls, msg, "text");
    lua_pushnumber(Ls, r);
    lua_setfield(Ls, msg, "r");
    lua_pushnumber(Ls, g);
    lua_setfield(Ls, msg, "g");
    lua_pushnumber(Ls, b);
    lua_setfield(Ls, msg, "b");
    lua_pushnumber(Ls, 1.0);
    lua_setfield(Ls, msg, "a");

    lua_pushnumber(Ls, openwow::core::GameClock::GetTickCountSeconds());
    lua_setfield(Ls, msg, "insertedAt");
    lua_pushinteger(Ls, line_id);
    lua_setfield(Ls, msg, "lineID");
    lua_pushinteger(Ls, back_fill ? 1 : 0);
    lua_setfield(Ls, msg, "backFill");
    lua_pushinteger(Ls, access_id);
    lua_setfield(Ls, msg, "accessID");
    lua_pushinteger(Ls, extra_data);
    lua_setfield(Ls, msg, "extraData");

    lua_Integer store_index = 1;
    if (back_fill) {
      if (n < max_lines) {
        for (lua_Integer index = n; index >= 1; --index) {
          lua_geti(Ls, msgs, index);
          lua_seti(Ls, msgs, index + 1);
        }
        ++n;
      }
    } else if (n < max_lines) {
      store_index = ++n;
    } else {
      for (lua_Integer index = 2; index <= n; ++index) {
        lua_geti(Ls, msgs, index);
        lua_seti(Ls, msgs, index - 1);
      }
      store_index = n;
    }
    lua_seti(Ls, msgs, store_index);

    lua_pushinteger(Ls, n);
    lua_setfield(Ls, 1, "__ow_smf_num_msg");

    lua_pop(Ls, 1);
    return 0;
  });
  lua_setfield(L, f, "AddMessage");

}

}

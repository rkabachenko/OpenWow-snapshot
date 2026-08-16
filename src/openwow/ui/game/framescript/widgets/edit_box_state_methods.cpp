#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/core/frame_font_binding.h"
#include "openwow/ui/game/framescript/widgets/font_string_state_methods.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/widgets/simple_edit_box.h"
#include "openwow/foundation/text/utf8.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include <lua.hpp>
#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {
using detail::ScriptReadBoolArgOrDefault;
void ApplyEditBoxStateMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_getfield(L, f, "__ow_eb_input_language");
  if (!lua_isstring(L, -1)) {
    lua_pop(L, 1);
    lua_pushstring(L, openwow::ui::widgets::EditBoxInputLanguageToken(
                          openwow::ui::widgets::EditBoxInputLanguage::Roman));
    lua_setfield(L, f, "__ow_eb_input_language");
  } else {
    lua_pop(L, 1);
  }

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls,
                      ScriptReadBoolArgOrDefault(Ls, 2, true) ? 1 : 0);
      lua_setfield(Ls, 1, "__ow_eb_password");
      SyncEditBoxInternalDisplayText(Ls, 1);
    }
    return 0;
  });
  lua_setfield(L, f, "SetPassword");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_password");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  });
  lua_setfield(L, f, "IsPassword");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_isnumber(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetBlinkSpeed(speed)",
                        GetObjectNameOrUnnamed(Ls, 1).c_str());
    }
    if (lua_istable(Ls, 1)) {

      lua_pushnumber(Ls, lua_tonumber(Ls, 2));
      lua_setfield(Ls, 1, "__ow_eb_blink");
    }
    return 0;
  });
  lua_setfield(L, f, "SetBlinkSpeed");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0.5);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_blink");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0.5);
    }
    return 1;
  });
  lua_setfield(L, f, "GetBlinkSpeed");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      if (lua_istable(Ls, 2)) {
        SetBoundFontObject(Ls, 1, 2);
        CopyNamedFontObjectStyle(Ls, 1, 2);
      } else if (lua_isstring(Ls, 2)) {
        if (PushNamedFontObject(Ls, lua_tostring(Ls, 2))) {
          SetBoundFontObject(Ls, 1, -1);
          CopyNamedFontObjectStyle(Ls, 1, -1);
          lua_pop(Ls, 1);
        } else {
          lua_pop(Ls, 1);
        }
      }
    }
    return 0;
  });
  lua_setfield(L, f, "SetFontObject");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_font_object");
    return 1;
  });
  lua_setfield(L, f, "GetFontObject");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return SetTableJustifyField(Ls, "EditBox", "__ow_justifyH", "SetJustifyH", true);
  });
  lua_setfield(L, f, "SetJustifyH");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    return PushTableJustify(Ls, "__ow_justifyH", "LEFT", true);
  });
  lua_setfield(L, f, "GetJustifyH");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_gettop(Ls) != 2 || !lua_isnumber(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetMaxBytes(max)",
                        GetObjectNameOrUnnamed(Ls, 1).c_str());
    }
    if (lua_istable(Ls, 1)) {
      auto max = static_cast<int>(lua_tonumber(Ls, 2));
      lua_pushnumber(Ls, static_cast<lua_Number>(max <= 0 ? 0 : max));
      lua_setfield(Ls, 1, "__ow_eb_maxbytes");
    }
    return 0;
  });
  lua_setfield(L, f, "SetMaxBytes");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_maxbytes");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  });
  lua_setfield(L, f, "GetMaxBytes");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_eb_countinvis");
    }
    return 0;
  });
  lua_setfield(L, f, "SetCountInvisibleLetters");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
    lua_setfield(Ls, 1, "__ow_eb_multiline");
    return 0;
  });
  lua_setfield(L, f, "SetMultiLine");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_multiline");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  });
  lua_setfield(L, f, "IsMultiLine");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      return 4;
    }

    lua_getfield(Ls, 1, "__ow_eb_inset_l");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 0); }

    lua_getfield(Ls, 1, "__ow_eb_inset_r");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 0); }

    lua_getfield(Ls, 1, "__ow_eb_inset_t");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 0); }

    lua_getfield(Ls, 1, "__ow_eb_inset_b");
    if (!lua_isnumber(Ls, -1)) { lua_pop(Ls, 1); lua_pushnumber(Ls, 0); }
    return 4;
  });
  lua_setfield(L, f, "GetTextInsets");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_maxletters");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  });
  lua_setfield(L, f, "GetMaxLetters");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_text");
    if (lua_isstring(Ls, -1)) {
      size_t len = 0;
      const char *str = lua_tolstring(Ls, -1, &len);
      lua_pop(Ls, 1);

      lua_getfield(Ls, 1, "__ow_eb_countinvis");
      const bool count_invisible = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      int letter_count;
      if (count_invisible) {
        letter_count = openwow::text::Utf8CodepointCount(
            std::string_view(str, len));
      } else {
        letter_count = openwow::ui::glue::CountEditBoxVisibleLetters(
            std::string_view(str, len));
      }
      lua_pushnumber(Ls, static_cast<lua_Number>(letter_count));
    } else {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  });
  lua_setfield(L, f, "GetNumLetters");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, openwow::ui::widgets::EditBoxInputLanguageToken(
                             openwow::ui::widgets::EditBoxInputLanguage::Roman));
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_input_language");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, openwow::ui::widgets::EditBoxInputLanguageToken(
                             openwow::ui::widgets::EditBoxInputLanguage::Roman));
    }
    return 1;
  });
  lua_setfield(L, f, "GetInputLanguage");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      lua_getfield(Ls, 1, "__ow_name");
      const char *name = lua_isstring(Ls, -1) ? lua_tostring(Ls, -1) : "<unnamed>";
      return luaL_error(Ls, "Usage: %s:SetCursorPosition(position)", name);
    }
    lua_pushnumber(Ls, static_cast<lua_Number>(
                           static_cast<int>(lua_tonumber(Ls, 2))));
    lua_setfield(Ls, 1, "__ow_eb_cursor");
    return 0;
  });
  lua_setfield(L, f, "SetCursorPosition");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_cursor");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  });
  lua_setfield(L, f, "GetCursorPosition");

  lua_pushcfunction(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_cursor");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 1;
  });
  lua_setfield(L, f, "GetUTF8CursorPosition");
}

}

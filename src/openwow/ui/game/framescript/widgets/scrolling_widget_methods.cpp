#include "openwow/ui/game/framescript/widgets/scrolling_widget_methods.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <lua.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "openwow/foundation/text/ascii.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/button_method_support.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_enum_helpers.h"

namespace openwow::ui::game::frame_api {

bool SetScrollFrameOffsetState(lua_State* L, int frame_index,
                               const bool horizontal, const double offset,
                               const bool invoke_script) {
  frame_index = lua_absindex(L, frame_index);
  const double normalized_offset = std::isfinite(offset) ? offset : 0.0;
  const char* field = horizontal ? "__ow_sf_hscroll" : "__ow_sf_vscroll";

  lua_getfield(L, frame_index, field);
  const double previous = lua_isnumber(L, -1) != 0 ? lua_tonumber(L, -1) : 0.0;
  lua_pop(L, 1);

  lua_pushnumber(L, normalized_offset);
  lua_setfield(L, frame_index, field);

  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
      manager != nullptr) {
    manager->frame_store().SetScrollOffset(
        openwow::ui::BorrowRawLuaStringField(L, frame_index,
                                             kLuaFrameRuntimeKeyField),
        horizontal, static_cast<float>(normalized_offset));
  }

  constexpr double kScrollEpsilon = 9.5367431640625e-7;
  if (!invoke_script ||
      std::fabs(normalized_offset - previous) < kScrollEpsilon) {
    return false;
  }

  lua_pushnumber(L, normalized_offset);
  const auto invocation = ::openwow::ui::game::InvokeFrameScriptHandler(
      L, frame_index, horizontal ? "OnHorizontalScroll" : "OnVerticalScroll",
      1);
  if (invocation.status != LUA_OK) {
    lua_pop(L, 1);
  }
  return true;
}

void ApplyScrollFrameMethods(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushnumber(L, 0);
  lua_setfield(L, f, "__ow_sf_vscroll");
  lua_pushnumber(L, 0);
  lua_setfield(L, f, "__ow_sf_hscroll");

  lua_pushcfunction(L, LuaScrollFrame_SetScrollChild);
  lua_setfield(L, f, "SetScrollChild");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnil(Ls);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sf_child");
        return 1;
      },
      0);
  lua_setfield(L, f, "GetScrollChild");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, 0);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sf_vscroll");
        if (!lua_isnumber(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 0);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetVerticalScroll");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        const int self_idx = ValidateFrameSelf(Ls);
        if (detail::LuaFrameMutationBlocked(Ls, 1)) {
          return 0;
        }
        if (lua_isnumber(Ls, 2) == 0) {
          return luaL_error(Ls, "Usage: %s:SetVerticalScroll(offset)",
                            lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
        }
        SetScrollFrameOffsetState(Ls, self_idx, false, lua_tonumber(Ls, 2),
                                  true);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetVerticalScroll");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, 0);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sf_hscroll");
        if (!lua_isnumber(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 0);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetHorizontalScroll");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        const int self_idx = ValidateFrameSelf(Ls);
        if (detail::LuaFrameMutationBlocked(Ls, 1)) {
          return 0;
        }
        if (lua_isnumber(Ls, 2) == 0) {
          return luaL_error(Ls, "Usage: %s:SetHorizontalScroll(offset)",
                            lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
        }
        SetScrollFrameOffsetState(Ls, self_idx, true, lua_tonumber(Ls, 2),
                                  true);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetHorizontalScroll");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        lua_pushnumber(Ls, 0);
        return 1;
      },
      0);
  lua_setfield(L, f, "GetVerticalScrollRange");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        lua_pushnumber(Ls, 0);
        return 1;
      },
      0);
  lua_setfield(L, f, "GetHorizontalScrollRange");

  lua_pushcclosure(L, [](lua_State* ) -> int { return 0; }, 0);
  lua_setfield(L, f, "UpdateScrollChildRect");
}

void ApplyScrollingMessageFrameMethods(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushinteger(L, 0);
  lua_setfield(L, f, "__ow_smf_scroll");
  lua_pushinteger(L, 0);
  lua_setfield(L, f, "__ow_smf_num_msg");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;

        lua_getfield(Ls, 1, "__ow_smf_num_msg");
        lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_pushinteger(Ls, n + 1);
        lua_setfield(Ls, 1, "__ow_smf_num_msg");
        return 0;
      },
      0);
  lua_setfield(L, f, "AddMessage");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        const lua_Integer max_lines = luaL_checkinteger(Ls, 2);
        if (max_lines <= 0) {
          return luaL_error(Ls, "SetMaxLines expects a positive line count");
        }

        lua_pushinteger(Ls, max_lines);
        lua_setfield(Ls, 1, "__ow_smf_maxlines");
        lua_newtable(Ls);
        lua_setfield(Ls, 1, "__ow_smf_messages");
        lua_pushinteger(Ls, 0);
        lua_setfield(Ls, 1, "__ow_smf_num_msg");
        lua_pushinteger(Ls, 0);
        lua_setfield(Ls, 1, "__ow_smf_scroll");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetMaxLines");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushinteger(Ls, 128);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_smf_maxlines");
        if (!lua_isinteger(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushinteger(Ls, 128);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetMaxLines");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
          lua_setfield(Ls, 1, "__ow_smf_fading");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetFading");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 3));
          lua_setfield(Ls, 1, "__ow_smf_fadedur");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetFadeDuration");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 10));
          lua_setfield(Ls, 1, "__ow_smf_timevis");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetTimeVisible");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushvalue(Ls, 2);
          lua_setfield(Ls, 1, "__ow_smf_insert");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetInsertMode");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushinteger(Ls, 0);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        if (!lua_isinteger(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushinteger(Ls, 0);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetCurrentLine");
  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushinteger(Ls, 0);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        if (!lua_isinteger(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushinteger(Ls, 0);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetCurrentScroll");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushinteger(Ls, 0);
          return 1;
        }
        const lua_Integer access_id =
            lua_isnumber(Ls, 2) != 0 ? lua_tointeger(Ls, 2) : 0;
        lua_getfield(Ls, 1, "__ow_smf_num_msg");
        const lua_Integer count =
            lua_isnumber(Ls, -1) != 0 ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        if (access_id == 0 || count <= 0) {
          lua_pushinteger(Ls, std::max<lua_Integer>(0, count));
          return 1;
        }

        lua_Integer matches = 0;
        lua_getfield(Ls, 1, "__ow_smf_messages");
        if (lua_istable(Ls, -1) != 0) {
          for (lua_Integer index = 1; index <= count; ++index) {
            lua_geti(Ls, -1, index);
            if (lua_istable(Ls, -1) != 0) {
              lua_getfield(Ls, -1, "accessID");
              matches += lua_isnumber(Ls, -1) != 0 &&
                         lua_tointeger(Ls, -1) == access_id;
              lua_pop(Ls, 1);
            }
            lua_pop(Ls, 1);
          }
        }
        lua_pop(Ls, 1);
        lua_pushinteger(Ls, matches);
        return 1;
      },
      0);
  lua_setfield(L, f, "GetNumMessages");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        lua_pushinteger(Ls, 0);
        return 1;
      },
      0);
  lua_setfield(L, f, "GetNumLinesDisplayed");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_Integer offset =
              static_cast<lua_Integer>(luaL_optinteger(Ls, 2, 0));
          if (offset < 0) offset = 0;
          lua_pushinteger(Ls, offset);
          lua_setfield(Ls, 1, "__ow_smf_scroll");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetScrollOffset");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        lua_Integer off = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_getfield(Ls, 1, "__ow_smf_num_msg");
        lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        off += 2;
        if (n > 0 && off >= n) off = n - 1;
        lua_pushinteger(Ls, off);
        lua_setfield(Ls, 1, "__ow_smf_scroll");
        return 0;
      },
      0);
  lua_setfield(L, f, "ScrollUp");
  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        lua_Integer off = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        off -= 2;
        if (off < 0) off = 0;
        lua_pushinteger(Ls, off);
        lua_setfield(Ls, 1, "__ow_smf_scroll");
        return 0;
      },
      0);
  lua_setfield(L, f, "ScrollDown");
  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        lua_getfield(Ls, 1, "__ow_smf_num_msg");
        lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_pushinteger(Ls, n > 0 ? n - 1 : 0);
        lua_setfield(Ls, 1, "__ow_smf_scroll");
        return 0;
      },
      0);
  lua_setfield(L, f, "ScrollToTop");
  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        lua_pushinteger(Ls, 0);
        lua_setfield(Ls, 1, "__ow_smf_scroll");
        return 0;
      },
      0);
  lua_setfield(L, f, "ScrollToBottom");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, 1);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        lua_Integer off = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_getfield(Ls, 1, "__ow_smf_num_msg");
        lua_Integer n = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_pushboolean(Ls, off >= (n > 0 ? n - 1 : 0));
        return 1;
      },
      0);
  lua_setfield(L, f, "AtTop");
  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, 1);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_smf_scroll");
        lua_Integer off = lua_isinteger(Ls, -1) ? lua_tointeger(Ls, -1) : 0;
        lua_pop(Ls, 1);
        lua_pushboolean(Ls, off == 0);
        return 1;
      },
      0);
  lua_setfield(L, f, "AtBottom");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_newtable(Ls);
          lua_setfield(Ls, 1, "__ow_smf_messages");
          lua_pushinteger(Ls, 0);
          lua_setfield(Ls, 1, "__ow_smf_num_msg");
          lua_pushinteger(Ls, 0);
          lua_setfield(Ls, 1, "__ow_smf_scroll");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "Clear");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushvalue(Ls, 2);
          lua_setfield(Ls, 1, "__ow_justifyH");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetJustifyH");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1) == 0) {
          return luaL_error(Ls,
                            "Attempt to find 'this' in non-table object (used "
                            "'.' instead of ':' ?)");
        }
        lua_getfield(Ls, 1, "__ow_justifyH");
        const char* stored = lua_tostring(Ls, -1);
        if (stored == nullptr || *stored == '\0') {
          lua_pop(Ls, 1);
          lua_pushstring(Ls, "LEFT");
          return 1;
        }
        uint32_t flags = 0;
        const int parsed =
            openwow::ui::StringToHorizontalJustify(stored, &flags);
        lua_pop(Ls, 1);
        lua_pushstring(Ls, openwow::ui::HorizontalJustifyFlagsToString(
                               parsed ? flags : 0));
        return 1;
      },
      0);
  lua_setfield(L, f, "GetJustifyH");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushvalue(Ls, 2);
          lua_setfield(Ls, 1, "__ow_justifyV");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetJustifyV");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1) == 0) {
          return luaL_error(Ls,
                            "Attempt to find 'this' in non-table object (used "
                            "'.' instead of ':' ?)");
        }
        lua_getfield(Ls, 1, "__ow_justifyV");
        const char* stored = lua_tostring(Ls, -1);
        if (stored == nullptr || *stored == '\0') {
          lua_pop(Ls, 1);
          lua_pushstring(Ls, "MIDDLE");
          return 1;
        }
        uint32_t flags = 0;
        const int parsed = openwow::ui::StringToVerticalJustify(stored, &flags);
        lua_pop(Ls, 1);
        lua_pushstring(
            Ls, openwow::ui::VerticalJustifyFlagsToString(parsed ? flags : 0));
        return 1;
      },
      0);
  lua_setfield(L, f, "GetJustifyV");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        const int self = ValidateFrameObjectSelf(Ls, "ScrollingMessageFrame");
        return SharedSetFontWorker(
            Ls, self, lua_adapter::ScriptObjectDisplayName(Ls, self));
      },
      0);
  lua_setfield(L, f, "SetFont");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        lua_getfield(Ls, 1, "__ow_font_path");
        lua_getfield(Ls, 1, "__ow_font_size");
        lua_getfield(Ls, 1, "__ow_font_flags");
        return 3;
      },
      0);
  lua_setfield(L, f, "GetFont");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        return SetPackedTextColorForTypedObject(Ls, "ScrollingMessageFrame");
      },
      0);
  lua_setfield(L, f, "SetTextColor");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        return GetPackedTextColorForTypedObject(Ls, "ScrollingMessageFrame");
      },
      0);
  lua_setfield(L, f, "GetTextColor");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        SetIndentedWordWrapForTypedObject(Ls, "ScrollingMessageFrame",
                                          "__ow_indented_wrap");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetIndentedWordWrap");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        return GetIndentedWordWrapForTypedObject(Ls, "ScrollingMessageFrame",
                                                 "__ow_indented_wrap");
      },
      0);
  lua_setfield(L, f, "GetIndentedWordWrap");

  lua_pushcfunction(L, SetShadowOffsetForSharedFontObject);
  lua_setfield(L, f, "SetShadowOffset");

  lua_pushcfunction(L, GetShadowOffsetForSharedFontObject);
  lua_setfield(L, f, "GetShadowOffset");

  lua_pushcfunction(L, SetPackedShadowColorForSharedFontObject);
  lua_setfield(L, f, "SetShadowColor");

  lua_pushcfunction(L, GetPackedShadowColorForSharedFontObject);
  lua_setfield(L, f, "GetShadowColor");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3) ||
            !lua_isnumber(Ls, 4) || !lua_isnumber(Ls, 5))
          return 0;
        const auto colorId = static_cast<int32_t>(lua_tonumber(Ls, 2));
        if (colorId == 0) return 0;

        auto clamp01 = [](double v) -> float {
          return static_cast<float>(std::min(std::max(v, 0.0), 1.0));
        };
        const float r = clamp01(lua_tonumber(Ls, 3));
        const float g = clamp01(lua_tonumber(Ls, 4));
        const float b = clamp01(lua_tonumber(Ls, 5));

        char key[64];
        std::snprintf(key, sizeof(key), "__ow_smf_colorid_%d_r", colorId);
        lua_pushnumber(Ls, r);
        lua_setfield(Ls, 1, key);
        std::snprintf(key, sizeof(key), "__ow_smf_colorid_%d_g", colorId);
        lua_pushnumber(Ls, g);
        lua_setfield(Ls, 1, key);
        std::snprintf(key, sizeof(key), "__ow_smf_colorid_%d_b", colorId);
        lua_pushnumber(Ls, b);
        lua_setfield(Ls, 1, key);
        return 0;
      },
      0);
  lua_setfield(L, f, "UpdateColorByID");
}

void ApplySliderMethods(lua_State* L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3)) {
          return luaL_error(Ls, "Usage: %s:SetMinMaxValues(min, max)",
                            lua_adapter::ScriptObjectDisplayName(Ls, 1));
        }
        const double mn = lua_tonumber(Ls, 2);
        const double mx = lua_tonumber(Ls, 3);
        if (mn > mx) {
          return luaL_error(Ls, "Usage: %s:SetMinMaxValues(min, max)",
                            lua_adapter::ScriptObjectDisplayName(Ls, 1));
        }
        lua_pushnumber(Ls, mn);
        lua_setfield(Ls, 1, "__ow_sl_min");
        lua_pushnumber(Ls, mx);
        lua_setfield(Ls, 1, "__ow_sl_max");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetMinMaxValues");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, 0);
          lua_pushnumber(Ls, 100);
          return 2;
        }
        lua_pushnumber(Ls, openwow::ui::ReadLuaNumberFieldOrDefault(
                               Ls, 1, "__ow_sl_min", 0.0));
        lua_pushnumber(Ls, openwow::ui::ReadLuaNumberFieldOrDefault(
                               Ls, 1, "__ow_sl_max", 100.0));
        return 2;
      },
      0);
  lua_setfield(L, f, "GetMinMaxValues");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;

        const double input = luaL_optnumber(Ls, 2, 0);

        const double sl_min =
            openwow::ui::ReadLuaNumberFieldOrDefault(Ls, 1, "__ow_sl_min", 0.0);
        const double sl_max = openwow::ui::ReadLuaNumberFieldOrDefault(
            Ls, 1, "__ow_sl_max", 100.0);
        const double sl_step = openwow::ui::ReadLuaNumberFieldOrDefault(
            Ls, 1, "__ow_sl_step", 1.0);
        const double old_val =
            openwow::ui::ReadLuaNumberFieldOrDefault(Ls, 1, "__ow_sl_val", 0.0);

        double clamped = input;
        if (clamped < sl_min) clamped = sl_min;
        if (clamped > sl_max) clamped = sl_max;

        double snapped = clamped;
        if (sl_step != 0.0) {
          const double rel = clamped - sl_min;
          const double half = sl_step * 0.5;
          if (rel > 0.0)
            snapped =
                static_cast<double>(static_cast<int>((rel + half) / sl_step)) *
                    sl_step +
                sl_min;
          else
            snapped =
                static_cast<double>(static_cast<int>((rel - half) / sl_step)) *
                    sl_step +
                sl_min;
        }

        if (sl_max < snapped + sl_step) snapped = sl_max;
        if (snapped - sl_step < sl_min) snapped = sl_min;

        lua_pushnumber(Ls, snapped);
        lua_setfield(Ls, 1, "__ow_sl_val");

        constexpr double kEps = 0.00000023841858;
        if (std::fabs(snapped - old_val) >= kEps) {
          lua_pushnumber(Ls, snapped);
          const auto invocation =
              InvokeFrameScriptHandler(Ls, 1, "OnValueChanged", 1);
          if (invocation.status != LUA_OK) lua_pop(Ls, 1);
        }

        return 0;
      },
      0);
  lua_setfield(L, f, "SetValue");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, 0);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sl_val");
        if (!lua_isnumber(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 0);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetValue");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, luaL_optnumber(Ls, 2, 1));
          lua_setfield(Ls, 1, "__ow_sl_step");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "SetValueStep");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnumber(Ls, 1);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sl_step");
        if (!lua_isnumber(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 1);
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetValueStep");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) return 0;
        if (!lua_isstring(Ls, 2)) {
          const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
          return luaL_error(Ls, "Usage: %s:SetOrientation(\"orientation\")",
                            name);
        }
        const char* orient = lua_tostring(Ls, 2);
        if (!orient) orient = "";
        const bool valid =
            openwow::text::EqualsIgnoreCaseAscii(orient, "HORIZONTAL") ||
            openwow::text::EqualsIgnoreCaseAscii(orient, "VERTICAL");
        if (!valid) {
          const char* name = lua_adapter::ScriptObjectDisplayName(Ls, 1);
          return luaL_error(Ls, "%s:SetOrientation(): Unknown orientation: %s",
                            name, orient);
        }
        lua_pushvalue(Ls, 2);
        lua_setfield(Ls, 1, "__ow_sl_orient");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetOrientation");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushstring(Ls, "HORIZONTAL");
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sl_orient");
        if (!lua_isstring(Ls, -1)) {
          lua_pop(Ls, 1);
          lua_pushstring(Ls, "HORIZONTAL");
        }
        return 1;
      },
      0);
  lua_setfield(L, f, "GetOrientation");

  InstallNativeTextureSlotMethods(
      L, f, "ThumbTexture", "__ow_sl_thumb",
      openwow::ui::framexml::UiFrame::TextureRole::SliderThumb, "ARTWORK");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (detail::LuaFrameMutationBlocked(Ls, 1)) {
          return 0;
        }
        if (lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, 1);
          lua_setfield(Ls, 1, "__ow_sl_enabled");
          FireScript(Ls, 1, "OnEnable");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "Enable");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (detail::LuaFrameMutationBlocked(Ls, 1)) {
          return 0;
        }
        if (lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, 0);
          lua_setfield(Ls, 1, "__ow_sl_enabled");
          FireScript(Ls, 1, "OnDisable");
        }
        return 0;
      },
      0);
  lua_setfield(L, f, "Disable");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushboolean(Ls, 1);
          return 1;
        }
        lua_getfield(Ls, 1, "__ow_sl_enabled");
        lua_pushboolean(Ls, lua_isboolean(Ls, -1) ? lua_toboolean(Ls, -1) : 1);
        lua_remove(Ls, -2);
        return 1;
      },
      0);
  lua_setfield(L, f, "IsEnabled");
}

static void SetCooldownBooleanField(lua_State* L, const char* field_name) {
  const int self_index = ValidateFrameObjectSelf(L, "Cooldown");
  lua_pushboolean(L, detail::ScriptReadBoolArgOrDefault(L, 2, true) ? 1 : 0);
  lua_setfield(L, self_index, field_name);
}

static int GetCooldownBooleanField(lua_State* L, const char* field_name) {
  const int self_index = ValidateFrameObjectSelf(L, "Cooldown");
  lua_getfield(L, self_index, field_name);
  lua_pushboolean(L, lua_toboolean(L, -1) != 0);
  lua_remove(L, -2);
  return 1;
}

void ApplyCooldownMethods(lua_State* L) {
  const int f = lua_absindex(L, -1);

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        const int self_index = ValidateFrameObjectSelf(Ls, "Cooldown");
        if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
          return luaL_error(
              Ls, "Usage: %s:SetCooldown(start, duration)",
              lua_adapter::ScriptObjectDisplayName(Ls, self_index));
        }

        const auto start = lua_tonumber(Ls, 2);
        const auto duration = lua_tonumber(Ls, 3);
        openwow::ui::game::detail::ApplyCooldownScriptState(
            Ls, self_index, start, duration, true);
        return 0;
      },
      0);
  lua_setfield(L, f, "SetCooldown");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        SetCooldownBooleanField(Ls, "__ow_cd_reverse");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetReverse");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        return GetCooldownBooleanField(Ls, "__ow_cd_reverse");
      },
      0);
  lua_setfield(L, f, "GetReverse");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        SetCooldownBooleanField(Ls, "__ow_cd_draw_edge");
        return 0;
      },
      0);
  lua_setfield(L, f, "SetDrawEdge");

  lua_pushcclosure(
      L,
      [](lua_State* Ls) -> int {
        return GetCooldownBooleanField(Ls, "__ow_cd_draw_edge");
      },
      0);
  lua_setfield(L, f, "GetDrawEdge");
}

}

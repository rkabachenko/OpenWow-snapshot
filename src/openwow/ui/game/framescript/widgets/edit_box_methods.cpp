#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/edit_box_methods.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/platform/adapters/ime/os_ime.h"
#include "openwow/ui/game/framescript/widgets/edit_box_state.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

void InvokeEditBoxScript(lua_State *L, const int edit_box_index,
                         const char *handler, const int argument_count) {
  const auto invocation = InvokeFrameScriptHandler(
      L, lua_absindex(L, edit_box_index), handler, argument_count);
  if (invocation.status != LUA_OK) {
    lua_pop(L, 1);
  }
}

void SyncEditBoxInternalDisplayText(lua_State *L, int edit_box_index) {
  if (L == nullptr || lua_istable(L, edit_box_index) == 0) {
    return;
  }
  edit_box_index = lua_absindex(L, edit_box_index);

  lua_getfield(L, edit_box_index, "__ow_eb_text");
  const char *raw_text = lua_isstring(L, -1) != 0 ? lua_tostring(L, -1) : "";
  std::string display_text = raw_text != nullptr ? raw_text : "";
  lua_pop(L, 1);

  lua_getfield(L, edit_box_index, "__ow_eb_password");
  const bool password = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (password) {
    display_text.assign(
        static_cast<std::size_t>(std::max(
            0, openwow::text::Utf8CodepointCount(display_text))),
        '*');
  }

  lua_getfield(L, edit_box_index, "__ow_eb_fontstr");
  if (lua_istable(L, -1) != 0) {
    lua_pushlstring(L, display_text.c_str(), display_text.size());
    lua_setfield(L, -2, "__ow_text");
  }
  lua_pop(L, 1);
}

void InitializeEditBoxInstanceDefaults(lua_State *L, int edit_box_index) {
  if (L == nullptr || lua_istable(L, edit_box_index) == 0) {
    return;
  }
  edit_box_index = lua_absindex(L, edit_box_index);

  lua_getfield(L, edit_box_index, "__ow_eb_initialized");
  const bool initialized = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (initialized) {
    return;
  }

  lua_pushboolean(L, 1);
  lua_setfield(L, edit_box_index, "__ow_eb_initialized");
  lua_pushstring(L, "");
  lua_setfield(L, edit_box_index, "__ow_eb_text");
  lua_pushinteger(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_cursor");
  lua_pushinteger(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_hl_start");
  lua_pushinteger(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_hl_end");
  lua_pushboolean(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_focus");
  lua_pushboolean(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_password");
  lua_pushinteger(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_hist_cap");
  lua_pushinteger(L, 0);
  lua_setfield(L, edit_box_index, "__ow_eb_hist_idx");
  lua_newtable(L);
  lua_setfield(L, edit_box_index, "__ow_eb_history");

  CreateFontStringTable(L, edit_box_index);
  lua_pushstring(L, "LEFT");
  lua_setfield(L, -2, "__ow_justifyH");
  lua_pushvalue(L, -1);
  lua_setfield(L, edit_box_index, "__ow_eb_fontstr");
  lua_pop(L, 1);
}

void ApplyEditBoxMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushstring(L, "");
  lua_setfield(L, f, "__ow_eb_text");
  lua_pushboolean(L, 0);
  lua_setfield(L, f, "__ow_eb_focus");
  lua_pushinteger(L, 0);
  lua_setfield(L, f, "__ow_eb_hist_cap");

  lua_pushinteger(L, 0);
  lua_setfield(L, f, "__ow_eb_hist_idx");

  lua_newtable(L);
  lua_setfield(L, f, "__ow_eb_history");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    if (!lua_isstring(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetText(\"text\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }

    lua_pushinteger(Ls, 0);
    lua_setfield(Ls, self, "__ow_eb_hl_start");
    lua_pushinteger(Ls, 0);
    lua_setfield(Ls, self, "__ow_eb_hl_end");
    const char *next = lua_tostring(Ls, 2);
    lua_getfield(Ls, self, "__ow_eb_text");
    const char *current = lua_tostring(Ls, -1);
    const bool changed = openwow::core::SStrCmpI(
                             next != nullptr ? next : "",
                             current != nullptr ? current : "", 0x7fffffffu) != 0;
    lua_pop(Ls, 1);
    if (!changed) {
      return 0;
    }

    lua_pushvalue(Ls, 2);
    lua_setfield(Ls, self, "__ow_eb_text");
    const auto length = static_cast<lua_Integer>(lua_rawlen(Ls, 2));
    lua_pushinteger(Ls, length);
    lua_setfield(Ls, self, "__ow_eb_cursor");
    SyncEditBoxInternalDisplayText(Ls, self);
    openwow::ui::game::QueueEditBoxDirtyState(Ls, self, true, false, true);
    InvokeEditBoxScript(Ls, self, "OnTextSet", 0);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushstring(Ls, "");
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_text");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "");
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    if (!lua_isnumber(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetNumber(number)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    const lua_Number number = lua_tonumber(Ls, 2);
    lua_pushnumber(Ls, number);
    lua_setfield(Ls, self, "__ow_eb_number");
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", number);
    lua_getfield(Ls, self, "SetText");
    lua_pushvalue(Ls, self);
    lua_pushstring(Ls, buffer);
    if (lua_pcall(Ls, 2, 0, 0) != LUA_OK) {
      return lua_error(Ls);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetNumber");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_text");
    const char *s = lua_tostring(Ls, -1);
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, s ? atof(s) : 0);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetNumber");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    if (!lua_isstring(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:Insert(\"text\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    const char *ins = lua_tostring(Ls, 2);
    const std::size_t insert_size = lua_rawlen(Ls, 2);
    lua_getfield(Ls, self, "__ow_eb_text");
    const char *cur = lua_tostring(Ls, -1);
    if (!cur)
      cur = "";
    std::string combined(cur);
    lua_pop(Ls, 1);

    const auto read_index = [&](const char *field, const std::size_t fallback) {
      lua_getfield(Ls, self, field);
      const auto value = lua_isnumber(Ls, -1)
                             ? static_cast<std::size_t>(std::max<lua_Integer>(
                                   0, lua_tointeger(Ls, -1)))
                             : fallback;
      lua_pop(Ls, 1);
      return std::min(value, combined.size());
    };
    std::size_t cursor = read_index("__ow_eb_cursor", combined.size());
    const std::size_t selection_start = read_index("__ow_eb_hl_start", cursor);
    const std::size_t selection_end = read_index("__ow_eb_hl_end", cursor);
    if (selection_start != selection_end) {
      const auto first = std::min(selection_start, selection_end);
      const auto last = std::max(selection_start, selection_end);
      combined.erase(first, last - first);
      cursor = first;
    }
    combined.insert(cursor, ins != nullptr ? std::string_view(ins, insert_size)
                                           : std::string_view());
    cursor += insert_size;
    lua_pushlstring(Ls, combined.data(), combined.size());
    lua_setfield(Ls, self, "__ow_eb_text");
    lua_pushinteger(Ls, static_cast<lua_Integer>(cursor));
    lua_setfield(Ls, self, "__ow_eb_cursor");
    lua_pushinteger(Ls, static_cast<lua_Integer>(cursor));
    lua_setfield(Ls, self, "__ow_eb_hl_start");
    lua_pushinteger(Ls, static_cast<lua_Integer>(cursor));
    lua_setfield(Ls, self, "__ow_eb_hl_end");
    SyncEditBoxInternalDisplayText(Ls, self);
    openwow::ui::game::QueueEditBoxDirtyState(Ls, self, true, false, true);
    lua_pushvalue(Ls, 2);
    InvokeEditBoxScript(Ls, self, "OnChar", 1);
    if (ins != nullptr && std::string_view(ins, insert_size).find(' ') !=
                              std::string_view::npos) {
      InvokeEditBoxScript(Ls, self, "OnSpacePressed", 0);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "Insert");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    if (!lua_isnumber(Ls, 2)) {
      lua_getfield(Ls, 1, "__ow_name");
      const char *name = lua_isstring(Ls, -1) ? lua_tostring(Ls, -1) : "<unnamed>";
      return luaL_error(Ls, "Usage: %s:SetCursorPosition(position)", name);
    }
    lua_getfield(Ls, 1, "__ow_eb_text");
    const std::size_t length = lua_isstring(Ls, -1) ? lua_rawlen(Ls, -1) : 0u;
    lua_pop(Ls, 1);
    const lua_Integer cursor = std::clamp<lua_Integer>(
        lua_tointeger(Ls, 2), 0, static_cast<lua_Integer>(length));
    lua_pushinteger(Ls, cursor);
    lua_setfield(Ls, 1, "__ow_eb_cursor");
    openwow::ui::game::QueueEditBoxDirtyState(Ls, 1, false, false, true);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetCursorPosition");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_cursor");
    if (!lua_isinteger(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushinteger(Ls, 0);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetCursorPosition");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;

    lua_getfield(Ls, 1, "__ow_eb_text");
    const char *txt = lua_isstring(Ls, -1) ? lua_tostring(Ls, -1) : "";
    const int textLen = static_cast<int>(std::strlen(txt));
    lua_pop(Ls, 1);

    const int start = static_cast<int>(luaL_optinteger(Ls, 2, 0));
    const int end   = static_cast<int>(luaL_optinteger(Ls, 3, -1));

    int hl_start = (start <= 0) ? 0 : start;
    if (hl_start >= textLen) hl_start = textLen;

    int hl_end;
    const int temp = (end <= -1) ? -1 : end;
    if (temp >= textLen) {
      hl_end = textLen;
    } else if (end <= -1) {
      hl_end = -1;
    } else {
      hl_end = end;
    }
    if (hl_end < hl_start) hl_end = textLen;

    lua_pushinteger(Ls, hl_start);
    lua_setfield(Ls, 1, "__ow_eb_hl_start");
    lua_pushinteger(Ls, hl_end);
    lua_setfield(Ls, 1, "__ow_eb_hl_end");
    return 0;
  }, 0);
  lua_setfield(L, f, "HighlightText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_isnumber(Ls, 2)) {
      return luaL_error(Ls, "Usage: %s:SetMaxLetters(max)",
                        lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }
    if (lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, static_cast<lua_Integer>(lua_tointeger(Ls, 2)));
      lua_setfield(Ls, 1, "__ow_eb_maxletters");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetMaxLetters");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_maxletters");
    if (!lua_isinteger(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushinteger(Ls, 0);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetMaxLetters");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_eb_multiline");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetMultiLine");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_multiline");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, f, "IsMultiLine");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    SetIndentedWordWrapForTypedObject(Ls, "EditBox", "__ow_eb_indented_wrap");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return GetIndentedWordWrapForTypedObject(Ls, "EditBox",
                                             "__ow_eb_indented_wrap");
  }, 0);
  lua_setfield(L, f, "GetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_eb_autofocus");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetAutoFocus");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_autofocus");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, f, "IsAutoFocus");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1) == 0) {
      return 0;
    }

    lua_getfield(Ls, LUA_REGISTRYINDEX, "openwow.set_focus");
    if (lua_isfunction(Ls, -1)) {
      if (const char *frame_key = GetFrameRuntimeKeyOrName(Ls, 1);
          frame_key != nullptr) {
        lua_pushstring(Ls, frame_key);
        (void)lua_pcall(Ls, 1, 0, 0);
      } else {
        lua_pop(Ls, 1);
      }
      return 0;
    }

    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_eb_focus");
    const bool already_focused = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    if (!already_focused &&
        openwow::ui::game::detail::GetLuaWidgetShownState(Ls, 1)) {
      lua_pushboolean(Ls, 1);
      lua_setfield(Ls, 1, "__ow_eb_focus");
      const auto invocation = InvokeFrameScriptHandler(
          Ls, 1, "OnEditFocusGained", 0);
      if (invocation.status != LUA_OK) lua_pop(Ls, 1);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetFocus");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1) == 0) {
      return 0;
    }

    lua_getfield(Ls, LUA_REGISTRYINDEX, "openwow.clear_focus");
    if (lua_isfunction(Ls, -1)) {
      if (const char *frame_key = GetFrameRuntimeKeyOrName(Ls, 1);
          frame_key != nullptr) {
        lua_pushstring(Ls, frame_key);
        (void)lua_pcall(Ls, 1, 0, 0);
      } else {
        lua_pop(Ls, 1);
      }
      return 0;
    }

    lua_pop(Ls, 1);
    lua_getfield(Ls, 1, "__ow_eb_focus");
    const bool had_focus = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    if (had_focus) {
      lua_pushboolean(Ls, 0);
      lua_setfield(Ls, 1, "__ow_eb_focus");
      const auto invocation = InvokeFrameScriptHandler(
          Ls, 1, "OnEditFocusLost", 0);
      if (invocation.status != LUA_OK) lua_pop(Ls, 1);
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "ClearFocus");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }

    lua_getfield(Ls, LUA_REGISTRYINDEX, "openwow.world_ui_runtime_context");
    auto *manager = static_cast<openwow::ui::game::runtime::WorldUiRuntimeContext *>(lua_touserdata(Ls, -1));
    lua_pop(Ls, 1);
    if (manager != nullptr) {
      const char *frame_key = GetFrameRuntimeKeyOrName(Ls, 1);
      const bool has_focus =
          frame_key != nullptr &&
          manager->input_router().focused_frame_name() == frame_key;
      lua_pushboolean(Ls, has_focus ? 1 : 0);
      return 1;
    }

    lua_getfield(Ls, 1, "__ow_eb_focus");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, f, "HasFocus");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    return SharedSetFontWorker(
        Ls, self, lua_adapter::ScriptObjectDisplayName(Ls, self));
  }, 0);
  lua_setfield(L, f, "SetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    lua_getfield(Ls, 1, "__ow_font_path");
    lua_getfield(Ls, 1, "__ow_font_size");
    lua_getfield(Ls, 1, "__ow_font_flags");
    return 3;
  }, 0);
  lua_setfield(L, f, "GetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetPackedTextColorForTypedObject(Ls, "EditBox");
  }, 0);
  lua_setfield(L, f, "SetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return GetPackedTextColorForTypedObject(Ls, "EditBox");
  }, 0);
  lua_setfield(L, f, "GetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    if (!lua_isnumber(Ls, 2) || !lua_isnumber(Ls, 3) || !lua_isnumber(Ls, 4) ||
        !lua_isnumber(Ls, 5)) {
      return luaL_error(Ls, "Usage: %s:SetTextInsets(l, r, t, b)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    lua_pushnumber(Ls, lua_tonumber(Ls, 2));
    lua_setfield(Ls, self, "__ow_eb_inset_l");
    lua_pushnumber(Ls, lua_tonumber(Ls, 3));
    lua_setfield(Ls, self, "__ow_eb_inset_r");
    lua_pushnumber(Ls, lua_tonumber(Ls, 4));
    lua_setfield(Ls, self, "__ow_eb_inset_t");
    lua_pushnumber(Ls, lua_tonumber(Ls, 5));
    lua_setfield(Ls, self, "__ow_eb_inset_b");
    NotifyFrameInputMutation(Ls, self, false);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetTextInsets");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      lua_pushnumber(Ls, 0);
      return 4;
    }
    lua_getfield(Ls, 1, "__ow_eb_inset_l");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    lua_getfield(Ls, 1, "__ow_eb_inset_r");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    lua_getfield(Ls, 1, "__ow_eb_inset_t");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    lua_getfield(Ls, 1, "__ow_eb_inset_b");
    if (!lua_isnumber(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 0);
    }
    return 4;
  }, 0);
  lua_setfield(L, f, "GetTextInsets");

  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, f, "EnableMouse");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    if (!lua_isnumber(Ls, 2) || lua_tointeger(Ls, 2) <= 0) {
      return luaL_error(
          Ls, "Usage: %s:SetHistoryLines(numLines)",
          lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }
    const int cap = static_cast<int>(lua_tointeger(Ls, 2));
    lua_pushinteger(Ls, cap);
    lua_setfield(Ls, 1, "__ow_eb_hist_cap");

    lua_getfield(Ls, 1, "__ow_eb_hist_idx");
    int idx = static_cast<int>(lua_tointeger(Ls, -1));
    lua_pop(Ls, 1);
    if (idx >= cap)
      idx = (cap > 0) ? cap - 1 : 0;
    lua_pushinteger(Ls, idx);
    lua_setfield(Ls, 1, "__ow_eb_hist_idx");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetHistoryLines");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnumber(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_hist_cap");
    lua_pushnumber(Ls, lua_tonumber(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetHistoryLines");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    if (lua_gettop(Ls) < 2 || !lua_isstring(Ls, 2)) {
      return luaL_error(
          Ls, "Usage: %s:AddHistoryLine(\"text\")",
          lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }

    lua_getfield(Ls, 1, "__ow_eb_hist_cap");
    const int cap = static_cast<int>(lua_tointeger(Ls, -1));
    lua_pop(Ls, 1);
    if (cap <= 0)
      return 0;

    lua_getfield(Ls, 1, "__ow_eb_hist_idx");
    int idx = static_cast<int>(lua_tointeger(Ls, -1));
    lua_pop(Ls, 1);

    lua_getfield(Ls, 1, "__ow_eb_history");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_newtable(Ls);
      lua_pushvalue(Ls, -1);
      lua_setfield(Ls, 1, "__ow_eb_history");
    }
    const int hist = lua_gettop(Ls);

    lua_pushvalue(Ls, 2);
    lua_rawseti(Ls, hist, idx + 1);

    idx = (idx + 1) % cap;
    lua_pushinteger(Ls, idx);
    lua_setfield(Ls, 1, "__ow_eb_hist_idx");

    lua_pop(Ls, 1);
    return 0;
  }, 0);
  lua_setfield(L, f, "AddHistoryLine");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1))
      return 0;
    lua_newtable(Ls);
    lua_setfield(Ls, 1, "__ow_eb_history");
    lua_pushinteger(Ls, 0);
    lua_setfield(Ls, 1, "__ow_eb_hist_idx");
    return 0;
  }, 0);
  lua_setfield(L, f, "ClearHistory");

  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, f, "SetBlinkSpeed");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_eb_numeric");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetNumeric");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, 0);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_numeric");
    lua_pushboolean(Ls, lua_toboolean(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, f, "IsNumeric");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true) ? 1 : 0);
    lua_setfield(Ls, self, "__ow_eb_password");
    SyncEditBoxInternalDisplayText(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetPassword");
  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, f, "SetCountInvisibleLetters");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      const bool enable = detail::ScriptReadBoolArgOrDefault(Ls, 2, true);
      lua_pushboolean(Ls, enable);
      lua_setfield(Ls, 1, "__ow_eb_alt_arrow_key");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetAltArrowKeyMode");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_alt_arrow_key");
    if (lua_toboolean(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetAltArrowKeyMode");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) return 0;
    lua_getfield(Ls, 1, "__ow_eb_input_lang");
    const int lang = lua_isnil(Ls, -1) ? 0 : static_cast<int>(lua_tointeger(Ls, -1));
    lua_pop(Ls, 1);
    openwow::platform::IME_ToggleNativeMode(lang == 0);
    return 0;
  }, 0);
  lua_setfield(L, f, "ToggleInputLanguage");

  lua_pushinteger(L, 0);
  lua_setfield(L, f, "__ow_eb_input_lang");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_eb_ime_composing");
    if (lua_toboolean(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "IsInIMECompositionMode");

  lua_pushboolean(L, 0);
  lua_setfield(L, f, "__ow_eb_ime_composing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "EditBox");
    lua_getfield(Ls, self, "__ow_eb_countinvis");
    const bool enabled = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    detail::lua_pushwowbool(Ls, enabled);
    return 1;
  }, 0);
  lua_setfield(L, f, "IsCountInvisibleLetters");
}

}

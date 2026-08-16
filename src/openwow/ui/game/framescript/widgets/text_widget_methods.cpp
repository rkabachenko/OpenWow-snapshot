#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/widgets/text_widget_methods.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/ui_enum_helpers.h"
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

void ApplyMessageFrameTextOverrides(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "MessageFrame");
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
    return SetPackedTextColorForTypedObject(Ls, "MessageFrame");
  }, 0);
  lua_setfield(L, f, "SetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return GetPackedTextColorForTypedObject(Ls, "MessageFrame");
  }, 0);
  lua_setfield(L, f, "GetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    SetIndentedWordWrapForTypedObject(Ls, "MessageFrame",
                                      "__ow_indented_wrap");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return GetIndentedWordWrapForTypedObject(Ls, "MessageFrame",
                                             "__ow_indented_wrap");
  }, 0);
  lua_setfield(L, f, "GetIndentedWordWrap");

}

const char *ConsumeSimpleHTMLElementSelector(lua_State *L) {
  if (lua_type(L, 2) != LUA_TSTRING) {
    return "p";
  }

  const char *element = lua_tostring(L, 2);
  if (element == nullptr) {
    return "p";
  }

  const char *canonical = nullptr;
  if (openwow::text::EqualsIgnoreCaseAscii(element, "P")) {
    canonical = "p";
  } else if (openwow::text::EqualsIgnoreCaseAscii(element, "H1")) {
    canonical = "h1";
  } else if (openwow::text::EqualsIgnoreCaseAscii(element, "H2")) {
    canonical = "h2";
  } else if (openwow::text::EqualsIgnoreCaseAscii(element, "H3")) {
    canonical = "h3";
  }

  if (canonical != nullptr) {
    lua_remove(L, 2);
    return canonical;
  }
  return "p";
}

int PushSimpleHTMLStyleTable(lua_State *L, int self_index, const char *element) {
  self_index = lua_absindex(L, self_index);
  lua_getfield(L, self_index, "__ow_html_styles");
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, self_index, "__ow_html_styles");
  }

  const int styles_index = lua_absindex(L, -1);
  lua_getfield(L, styles_index, element);
  if (lua_istable(L, -1) == 0) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, styles_index, element);
  }

  lua_remove(L, styles_index);
  return lua_absindex(L, -1);
}

int SetSimpleHTMLJustifyField(lua_State *L, const char *field_name,
                              const char *method_name) {
  const int self_index = ValidateFrameObjectSelf(L, "SimpleHTML");
  const char *element = ConsumeSimpleHTMLElementSelector(L);

  uint32_t flags = 0;
  const char *raw = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : nullptr;
  if (openwow::ui::JustifyStringToFlags(raw, &flags) == 0) {
    const char* usage_name = lua_adapter::ScriptObjectDisplayName(L, self_index);
    return luaL_error(L, "Usage: %s:%s(\"justify\")", usage_name, method_name);
  }

  const int style_index = PushSimpleHTMLStyleTable(L, self_index, element);
  lua_pushstring(L, openwow::ui::JustifyFlagsToString(flags));
  lua_setfield(L, style_index, field_name);
  return 0;
}

const char *GetSimpleHTMLJustifyField(lua_State *L, int self_index,
                                      const char *field_name,
                                      const char *default_value) {
  const char *element = ConsumeSimpleHTMLElementSelector(L);
  const int style_index = PushSimpleHTMLStyleTable(L, self_index, element);
  lua_getfield(L, style_index, field_name);
  const char *stored = lua_tostring(L, -1);
  if (stored == nullptr || *stored == '\0') {
    lua_pop(L, 1);
    return default_value;
  }
  return stored;
}

void ApplySimpleHTMLMethods(lua_State *L) {
  int f = lua_absindex(L, -1);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushvalue(Ls, 2);
      lua_setfield(Ls, 1, "__ow_html_text");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetText");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls,
                      openwow::ui::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_html_hyperlinks");
    }
    return 0;
  }, 0);
  lua_setfield(L, f, "SetHyperlinksEnabled");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_html_hyperlinks");
    if (lua_toboolean(Ls, -1) != 0) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetHyperlinksEnabled");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style = PushSimpleHTMLStyleTable(Ls, self, element);
    return SharedSetFontWorker(
        Ls, style, lua_adapter::ScriptObjectDisplayName(Ls, self));
  }, 0);
  lua_setfield(L, f, "SetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style = PushSimpleHTMLStyleTable(Ls, self, element);
    lua_getfield(Ls, style, "__ow_font_path");
    lua_getfield(Ls, style, "__ow_font_size");
    lua_getfield(Ls, style, "__ow_font_flags");
    return 3;
  }, 0);
  lua_setfield(L, f, "GetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    StorePackedTextColor(Ls, style_index);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    PushPackedTextColor(Ls, style_index);
    return 4;
  }, 0);
  lua_setfield(L, f, "GetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    StorePackedColor(Ls, style_index, kShadowColorFieldNames,
                     kShadowColorDefaults);
    return 0;
  }, 0);
  lua_setfield(L, f, "SetShadowColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    PushPackedColor(Ls, style_index, kShadowColorFieldNames,
                    kShadowColorDefaults);
    return 4;
  }, 0);
  lua_setfield(L, f, "GetShadowColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(Ls, "Usage: %s:SetShadowOffset(x, y)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_index));
    }
    StoreShadowOffsetForObject(
        Ls, style_index, static_cast<float>(lua_tonumber(Ls, 2)),
        static_cast<float>(lua_tonumber(Ls, 3)));
    return 0;
  }, 0);
  lua_setfield(L, f, "SetShadowOffset");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    PushShadowOffsetComponentForObject(Ls, style_index, "__ow_shadow_x");
    PushShadowOffsetComponentForObject(Ls, style_index, "__ow_shadow_y");
    return 2;
  }, 0);
  lua_setfield(L, f, "GetShadowOffset");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    if (lua_type(Ls, 2) != LUA_TSTRING) {
      lua_getfield(Ls, self_idx, "__ow_name");
      const char *name = lua_isstring(Ls, -1) ? lua_tostring(Ls, -1) : "<unnamed>";
      return luaL_error(Ls, "Usage: %s:SetHyperlinkFormat(\"format\")", name);
    }
    lua_pushvalue(Ls, 2);
    lua_setfield(Ls, self_idx, "__ow_html_hyperlink_format");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetHyperlinkFormat");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    lua_getfield(Ls, self_idx, "__ow_html_hyperlink_format");
    if (!lua_isstring(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushstring(Ls, "|H%s|h%s|h");
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetHyperlinkFormat");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetSpacing(spacing)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_index));
    }
    const float spacing_pixels = static_cast<float>(lua_tonumber(Ls, 2));
    lua_pushnumber(
        Ls, openwow::ui::PixelUiHorizontalCoordinateToStored(spacing_pixels));
    lua_setfield(Ls, style_index, "__ow_spacing");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetSpacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    lua_getfield(Ls, style_index, "__ow_spacing");
    if (lua_isnumber(Ls, -1) != 0) {
      const float stored_spacing = static_cast<float>(lua_tonumber(Ls, -1));
      lua_pop(Ls, 1);
      lua_pushnumber(
          Ls,
          openwow::ui::StoredUiHorizontalCoordinateToPixels(stored_spacing));
      return 1;
    }
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, 0);
    return 1;
  }, 0);
  lua_setfield(L, f, "GetSpacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    lua_pushboolean(Ls,
                    openwow::ui::ScriptReadBoolArgOrDefault(Ls, 2, true));
    lua_setfield(Ls, style_index, "__ow_indented_wrap");
    return 0;
  }, 0);
  lua_setfield(L, f, "SetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *element = ConsumeSimpleHTMLElementSelector(Ls);
    const int style_index = PushSimpleHTMLStyleTable(Ls, self_index, element);
    lua_getfield(Ls, style_index, "__ow_indented_wrap");
    const bool enabled = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    if (enabled) {
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, f, "GetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetSimpleHTMLJustifyField(Ls, "__ow_justifyH", "SetJustifyH");
  }, 0);
  lua_setfield(L, f, "SetJustifyH");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *stored = GetSimpleHTMLJustifyField(Ls, self, "__ow_justifyH", "CENTER");
    uint32_t flags = 0;
    const int parsed = openwow::ui::StringToHorizontalJustify(stored, &flags);
    lua_pop(Ls, 1);
    lua_pushstring(Ls, openwow::ui::HorizontalJustifyFlagsToString(parsed ? flags : 0));
    return 1;
  }, 0);
  lua_setfield(L, f, "GetJustifyH");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetSimpleHTMLJustifyField(Ls, "__ow_justifyV", "SetJustifyV");
  }, 0);
  lua_setfield(L, f, "SetJustifyV");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "SimpleHTML");
    const char *stored = GetSimpleHTMLJustifyField(Ls, self, "__ow_justifyV", "MIDDLE");
    uint32_t flags = 0;
    const int parsed = openwow::ui::StringToVerticalJustify(stored, &flags);
    lua_pop(Ls, 1);
    lua_pushstring(Ls, openwow::ui::VerticalJustifyFlagsToString(parsed ? flags : 0));
    return 1;
  }, 0);
  lua_setfield(L, f, "GetJustifyV");

  ApplyFrameScriptHandlerMethods(L, f, false);
}

}

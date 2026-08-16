#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_binding.h"
#include "openwow/ui/game/framescript/core/frame_font_face.h"
#include "openwow/ui/game/framescript/core/frame_method_table_runtime.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/lua_binding_registry.h"
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

void ApplyFontObjectMethods(lua_State *L, int table_index) {
  table_index = lua_absindex(L, table_index);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    const char* object_name = lua_adapter::ScriptObjectDisplayName(Ls, self);
    const int argument_type = lua_type(Ls, 2);

    if (argument_type == LUA_TTABLE) {
      if (!openwow::ui::game::lua_adapter::HasScriptObjectIdentity(Ls, 2)) {
        return luaL_error(
            Ls, "%s:SetFontObject(): Couldn't find 'this' in font object",
            object_name);
      }

      if (!openwow::ui::game::lua_adapter::HasCanonicalScriptObjectType(
              Ls, 2, openwow::ui::widgets::ScriptObjectType::Font)) {
        return luaL_error(
            Ls, "%s:SetFontObject(): Wrong object type, expected font",
            object_name);
      }

      if (WouldIntroduceFontObjectBindingCycle(Ls, self, 2)) {
        return luaL_error(
            Ls, "%s:SetFontObject(): Can't create a font object loop",
            object_name);
      }

      SetBoundFontObject(Ls, self, 2);
      CopyNamedFontObjectStyle(Ls, self, 2);
      return 0;
    }

    if (argument_type == LUA_TSTRING) {
      const char *font_name = lua_tostring(Ls, 2);
      if (PushNamedFontObject(Ls, lua_tostring(Ls, 2))) {
        if (WouldIntroduceFontObjectBindingCycle(Ls, self, -1)) {
          lua_pop(Ls, 1);
          return luaL_error(
              Ls, "%s:SetFontObject(): Can't create a font object loop",
              object_name);
        }
        SetBoundFontObject(Ls, self, -1);
        CopyNamedFontObjectStyle(Ls, self, -1);
        lua_pop(Ls, 1);
        return 0;
      }

      return luaL_error(Ls, "%s:SetFontObject(): Couldn't find font named %s",
                        object_name, font_name);
    }

    if (argument_type != LUA_TNONE && argument_type != LUA_TNIL) {
      return luaL_error(Ls, "Usage: %s:SetFontObject(font or \"font\" or nil)",
                        object_name);
    }

    ClearBoundFontObject(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, table_index, "SetFontObject");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    lua_getfield(Ls, self, "__ow_font_object");
    return 1;
  }, 0);
  lua_setfield(L, table_index, "GetFontObject");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    const char* object_name = lua_adapter::ScriptObjectDisplayName(Ls, self);
    const int argument_type = lua_type(Ls, 2);

    if (argument_type == LUA_TTABLE) {
      if (!openwow::ui::game::lua_adapter::HasScriptObjectIdentity(Ls, 2)) {
        return luaL_error(
            Ls, "%s:CopyFontObject(): Couldn't find 'this' in font object",
            object_name);
      }

      if (!openwow::ui::game::lua_adapter::HasCanonicalScriptObjectType(
              Ls, 2, openwow::ui::widgets::ScriptObjectType::Font)) {
        return luaL_error(
            Ls, "%s:CopyFontObject(): Wrong object type, expected font",
            object_name);
      }

      CopyFontObjectStateFromSource(Ls, self, 2);
      return 0;
    }

    if (argument_type == LUA_TSTRING) {
      const char *font_name = lua_tostring(Ls, 2);
      if (PushNamedFontObject(Ls, font_name)) {
        CopyFontObjectStateFromSource(Ls, self, -1);
        lua_pop(Ls, 1);
        return 0;
      }

      return luaL_error(Ls, "%s:CopyFontObject(): Couldn't find font named %s",
                        object_name, font_name);
    }

    return luaL_error(Ls, "Usage: %s:CopyFontObject(font or \"font\")",
                      object_name);
  }, 0);
  lua_setfield(L, table_index, "CopyFontObject");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    return SharedSetFontWorker(
        Ls, self, lua_adapter::ScriptObjectDisplayName(Ls, self));
  }, 0);
  lua_setfield(L, table_index, "SetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    lua_getfield(Ls, self, "__ow_font_path");
    lua_getfield(Ls, self, "__ow_font_size");
    lua_getfield(Ls, self, "__ow_font_flags");
    return 3;
  }, 0);
  lua_setfield(L, table_index, "GetFont");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateSharedFontObjectSelf(Ls);
    const auto alpha_byte = openwow::ui::game::QuantizeScriptAlphaByteWrapped(
        luaL_optnumber(Ls, 2, 1));
    StoreSharedFontAlphaByte(Ls, self, alpha_byte);
    PropagateSharedFontAlphaStyle(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, table_index, "SetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateSharedFontObjectSelf(Ls);
    lua_pushnumber(
        Ls, openwow::ui::game::NormalizeFrameAlphaByte(
                ReadSharedFontAlphaByteOrDefault(Ls, self)));
    return 1;
  }, 0);
  lua_setfield(L, table_index, "GetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    StorePackedTextColor(Ls, self);
    SyncSharedFontAlphaFromTextColor(Ls, self);
    PropagateSharedFontTextColorStyle(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, table_index, "SetTextColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    PushPackedTextColor(Ls, self);
    return 4;
  }, 0);
  lua_setfield(L, table_index, "GetTextColor");

  lua_pushcfunction(L, SetPackedShadowColorForSharedFontObject);
  lua_setfield(L, table_index, "SetShadowColor");

  lua_pushcfunction(L, GetPackedShadowColorForSharedFontObject);
  lua_setfield(L, table_index, "GetShadowColor");

  lua_pushcfunction(L, SetShadowOffsetForSharedFontObject);
  lua_setfield(L, table_index, "SetShadowOffset");

  lua_pushcfunction(L, GetShadowOffsetForSharedFontObject);
  lua_setfield(L, table_index, "GetShadowOffset");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int result = SetTableJustifyField(Ls, "Font", "__ow_justifyH", "SetJustifyH", true);
    PropagateSharedFontLayoutStyle(Ls, 1);
    return result;
  }, 0);
  lua_setfield(L, table_index, "SetJustifyH");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return PushFontObjectJustify(Ls, "__ow_justifyH", "CENTER", true);
  }, 0);
  lua_setfield(L, table_index, "GetJustifyH");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int result = SetTableJustifyField(Ls, "Font", "__ow_justifyV", "SetJustifyV", false);
    PropagateSharedFontLayoutStyle(Ls, 1);
    return result;
  }, 0);
  lua_setfield(L, table_index, "SetJustifyV");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return PushFontObjectJustify(Ls, "__ow_justifyV", "MIDDLE", false);
  }, 0);
  lua_setfield(L, table_index, "GetJustifyV");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    const float spacing_pixels = static_cast<float>(luaL_optnumber(Ls, 2, 0));
    lua_pushnumber(
        Ls, openwow::ui::PixelUiHorizontalCoordinateToStored(spacing_pixels));
    lua_setfield(Ls, self, "__ow_spacing");
    PropagateSharedFontSpacingStyle(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, table_index, "SetSpacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    lua_getfield(Ls, self, "__ow_spacing");
    if (lua_isnumber(Ls, -1) != 0) {
      const float stored_spacing =
          static_cast<float>(lua_tonumber(Ls, -1));
      lua_pop(Ls, 1);
      lua_pushnumber(
          Ls, openwow::ui::StoredUiHorizontalCoordinateToPixels(stored_spacing));
      return 1;
    }
    lua_pop(Ls, 1);
    lua_pushnumber(Ls, 0);
    return 1;
  }, 0);
  lua_setfield(L, table_index, "GetSpacing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    lua_pushboolean(Ls,
                    openwow::ui::ScriptReadBoolArgOrDefault(Ls, 2, true));
    lua_setfield(Ls, self, "__ow_indented_wrap");
    PropagateSharedFontLayoutStyle(Ls, self);
    return 0;
  }, 0);
  lua_setfield(L, table_index, "SetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    lua_getfield(Ls, self, "__ow_indented_wrap");
    if (lua_toboolean(Ls, -1) != 0) {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1.0);
    } else {
      lua_pop(Ls, 1);
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, table_index, "GetIndentedWordWrap");

  lua_pushcclosure(L, [](lua_State* Ls) -> int {
    return PushSharedFontObjectName(Ls);
  }, 0);
  lua_setfield(L, table_index, "GetName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameObjectSelf(Ls, "Font");
    lua_pushstring(Ls, "Font");
    return 1;
  }, 0);
  lua_setfield(L, table_index, "GetObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Font");
    if (lua_isstring(Ls, 2) == 0) {

      return luaL_error(Ls, "Usage: %s:IsObjectType(\"TYPE\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }

    const char *type_name = lua_tostring(Ls, 2);
    if (openwow::text::EqualsIgnoreCaseAscii(type_name, "Font")) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, table_index, "IsObjectType");
}

void RegisterFrameScriptMethods(lua_State *L) {
  RegisterFrameScriptMethodsImpl(L);
}

void ApplyRegisteredFrameMethods(lua_State *L) {
  ApplyRegisteredFrameMethodsImpl(L);
}

void ApplyFrameTypeMethods(lua_State *L, const char *frame_type) {
  ApplyFrameTypeMethodsImpl(L, frame_type);
}

int LuaCreateFont(lua_State *L) {

  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: CreateFont(\"name\")");
  }

  const char *name = lua_tostring(L, 1);
  if (PushNamedFontObject(L, name)) {
    return 1;
  }
  lua_pop(L, 1);

  lua_newtable(L);
  int f = lua_absindex(L, -1);

  lua_pushstring(L, "Font");
  lua_setfield(L, f, "__ow_type");
  openwow::ui::game::lua_adapter::AttachScriptObjectIdentity(L, f);

  if (name != nullptr) {
    lua_pushstring(L, name);
    lua_setfield(L, f, "__ow_name");
  }

  ApplyFontObjectMethods(L, f);
  ApplyCachedMethodTableAndStripFunctions(L, f, kFontObjectMethodTableRegistryKey);

  RegisterNamedFontObject(L, f, name);
  BindNamedFontObjectGlobalIfMissing(L, f, name);

  return 1;
}

bool PushNamedFontObject(lua_State *L, const char *name) {
  if (name == nullptr) {
    lua_pushnil(L);
    return false;
  }

  const std::string key = NormalizeNamedFontObjectKey(name);
  const int registry = EnsureNamedFontObjectRegistry(L);
  lua_getfield(L, registry, key.c_str());
  lua_remove(L, registry);
  return lua_istable(L, -1) != 0;
}

void RegisterNamedFontObject(lua_State *L, int font_index, const char *name) {
  if (name == nullptr) {
    return;
  }

  const std::string key = NormalizeNamedFontObjectKey(name);
  font_index = lua_absindex(L, font_index);
  const int registry = EnsureNamedFontObjectRegistry(L);
  lua_pushvalue(L, font_index);
  lua_setfield(L, registry, key.c_str());
  lua_pop(L, 1);
}

void ClearNamedFontObjectRegistry(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  lua_getfield(L, LUA_REGISTRYINDEX, kNamedFontObjectRegistryKey);
  if (lua_istable(L, -1) != 0) {
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      if (lua_istable(L, -1) != 0) {
        lua_getfield(L, -1, "__ow_name");
        const char *name = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (name != nullptr) {
          openwow::ui::UnregisterLuaGlobal(L, name);
        }
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, kNamedFontObjectRegistryKey);
}

void CopyNamedFontObjectStyle(lua_State *L, int target_index, int font_index) {
  target_index = lua_absindex(L, target_index);
  font_index = lua_absindex(L, font_index);

  if (FontObjectHasNonEmptyStringField(L, font_index, "__ow_font_path")) {
    CopyNamedFontObjectStyleGroup(
        L, target_index, font_index,
        {"__ow_font_path", "__ow_font_size", "__ow_text_height",
         "__ow_font_flags"});
  }

  if (FontObjectHasAnyStoredFields(
          L, font_index,
          {"__ow_justifyH", "__ow_justifyV", "__ow_indented_wrap"})) {
    CopyNamedFontObjectStyleGroup(
        L, target_index, font_index,
        {"__ow_justifyH", "__ow_justifyV", "__ow_indented_wrap"});
  }

  if (FontObjectHasAnyStoredFields(
          L, font_index,
          {"__ow_text_r", "__ow_text_g", "__ow_text_b", "__ow_text_a"})) {
    CopyNamedFontObjectStyleGroup(
        L, target_index, font_index,
        {"__ow_text_r", "__ow_text_g", "__ow_text_b", "__ow_text_a"});
  }

  if (FontObjectHasAnyStoredFields(
          L, font_index,
          {"__ow_shadow_r", "__ow_shadow_g", "__ow_shadow_b", "__ow_shadow_a",
           "__ow_shadow_x", "__ow_shadow_y"})) {
    CopyNamedFontObjectStyleGroup(
        L, target_index, font_index,
        {"__ow_shadow_r", "__ow_shadow_g", "__ow_shadow_b", "__ow_shadow_a",
         "__ow_shadow_x", "__ow_shadow_y"});
  }

  if (FontObjectHasStoredField(L, font_index, "__ow_spacing")) {
    openwow::ui::CopyLuaTableField(L, target_index, font_index, "__ow_spacing");
  }

  if (FontObjectHasStoredField(L, font_index, "__ow_alpha")) {
    openwow::ui::CopyLuaTableField(L, target_index, font_index, "__ow_alpha");
  }
}

void ClearBoundFontObjectByRef(lua_State* L, const int script_object_ref) {
  if (L == nullptr || script_object_ref < 0) {
    return;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, script_object_ref);
  if (lua_istable(L, -1) != 0) {
    ClearBoundFontObject(L, -1);
  }
  lua_pop(L, 1);
}

}

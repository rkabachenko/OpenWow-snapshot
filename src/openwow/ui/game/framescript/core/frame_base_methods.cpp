#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/framescript/core/frame_anchor_runtime.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_object_tree.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_region_factory.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/framescript/core/script_region_ownership.h"
#include "openwow/ui/game/framescript/core/frame_layout_methods.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_script_dispatch.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_table_field.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/frame_attribute_lua.h"
#include "openwow/ui/game/framescript/core/frame_event_methods.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/widgets/simple_frame.h"
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

void ApplyBaseFrameMethods(lua_State *L) {

  lua_pushcfunction(L, LuaFrame_RegisterEvent);
  lua_setfield(L, -2, "RegisterEvent");

  lua_pushcfunction(L, LuaFrame_IsEventRegistered);
  lua_setfield(L, -2, "IsEventRegistered");

  lua_pushcfunction(L, LuaFrame_UnregisterEvent);
  lua_setfield(L, -2, "UnregisterEvent");

  lua_pushcfunction(L, LuaFrame_UnregisterAllEvents);
  lua_setfield(L, -2, "UnregisterAllEvents");

  ApplyFrameScriptHandlerMethods(L, -1, true);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    if (detail::ShowLuaScriptFrame(Ls, self)) {
      NotifyFrameInputMutation(Ls, self, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "Show");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    if (detail::HideLuaScriptFrame(Ls, self)) {
      NotifyFrameInputMutation(Ls, self, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "Hide");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    lua_getfield(Ls, self, "__ow_visible");
    if (lua_isboolean(Ls, -1)) {
      int v = lua_toboolean(Ls, -1);
      lua_pop(Ls, 1);
      if (v)
        lua_pushnumber(Ls, 1);
      else
        lua_pushnil(Ls);
    } else {
      lua_pop(Ls, 1);
      lua_pushnumber(Ls, 1);
    }
    return 1;
  }, 0);
  lua_setfield(L, -2, "IsShown");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    if (detail::IsLuaWidgetEffectivelyVisible(Ls, self))
      lua_pushnumber(Ls, 1);
    else
      lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, -2, "IsVisible");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    lua_getfield(Ls, self, "__ow_name");
    const char *n = lua_tostring(Ls, -1);
    if (n && *n)
      return 1;
    lua_pop(Ls, 1);
    lua_pushnil(Ls);
    return 1;
  }, 0);
  lua_setfield(L, -2, "GetName");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameScriptSelf(Ls);
    lua_pushstring(Ls, GetLuaFrameRuntimeTypeName(Ls, self_index));
    return 1;
  }, 0);
  lua_setfield(L, -2, "GetObjectType");

  openwow::ui::anim::ApplyAnimationRegionMethods(L);

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetPointInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, -2, "SetPoint");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaClearAllPointsInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, -2, "ClearAllPoints");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return LuaSetAllPointsInternal(Ls, LuaAnchorTargetValidation::kRequireScriptObjectThis);
  }, 0);
  lua_setfield(L, -2, "SetAllPoints");

  lua_pushcfunction(L, LuaGetPointInternal);
  lua_setfield(L, -2, "GetPoint");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    const int anchors = EnsureLuaAnchorArray(Ls, self);
    NormalizeLuaAnchorArray(Ls, anchors);
    lua_pushinteger(Ls, CountVisibleLuaAnchors(Ls, anchors));
    lua_remove(Ls, anchors);
    return 1;
  }, 0);
  lua_setfield(L, -2, "GetNumPoints");

  lua_pushcclosure(L, [](lua_State *Ls) -> int { return SetLuaRegionSize(Ls); }, 0);
  lua_setfield(L, -2, "SetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetWidth", "width", "__ow_width");
  }, 0);
  lua_setfield(L, -2, "SetWidth");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    return SetLuaRegionDimension(Ls, "SetHeight", "height", "__ow_height");
  }, 0);
  lua_setfield(L, -2, "SetHeight");

  lua_pushcfunction(L, LuaRegion_GetWidth);
  lua_setfield(L, -2, "GetWidth");

  lua_pushcfunction(L, LuaRegion_GetHeight);
  lua_setfield(L, -2, "GetHeight");

  lua_pushcfunction(L, LuaRegion_GetSize);
  lua_setfield(L, -2, "GetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      return 0;
    }

    lua_pushnumber(Ls, rect.left);
    lua_pushnumber(Ls, rect.bottom);
    lua_pushnumber(Ls, rect.width);
    lua_pushnumber(Ls, rect.height);
    return 4;
  }, 0);
  lua_setfield(L, -2, "GetRect");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameLikeObjectSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameBoundsRect(Ls, self, &rect)) {
      return 0;
    }

    lua_pushnumber(Ls, rect.left);
    lua_pushnumber(Ls, rect.bottom);
    lua_pushnumber(Ls, rect.width);
    lua_pushnumber(Ls, rect.height);
    return 4;
  }, 0);
  lua_setfield(L, -2, "GetBoundsRect");

  lua_pushcfunction(L, LuaRegion_IsDragging);
  lua_setfield(L, -2, "IsDragging");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetAlpha(alpha 0 to 1)", lua_adapter::ScriptObjectDisplayName(Ls, 1));
    }

    const auto alpha_byte = openwow::ui::game::QuantizeFrameAlphaByteTruncated(lua_tonumber(Ls, 2));
    const auto current_alpha_byte = openwow::ui::game::QuantizeFrameAlphaByteTruncated(
        openwow::ui::ReadLuaNumberFieldOrDefault(Ls, 1, "__ow_alpha", 1.0));
    if (current_alpha_byte == alpha_byte) {
      return 0;
    }

    openwow::ui::WriteLuaNumberField(Ls, 1, "__ow_alpha", openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte));
    if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls); manager != nullptr) {
      if (const char *frame_key = GetFrameRuntimeKeyOrName(Ls, self_index);
          frame_key != nullptr && frame_key[0] != '\0') {
        manager->frame_store().SetFrameAlphaByte(frame_key, alpha_byte);
      }
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "SetAlpha");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    ValidateFrameSelf(Ls);
    const auto alpha_byte = openwow::ui::game::QuantizeFrameAlphaByteTruncated(
        openwow::ui::ReadLuaNumberFieldOrDefault(Ls, 1, "__ow_alpha", 1.0));
    lua_pushnumber(Ls, openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte));
    return 1;
  }, 0);
  lua_setfield(L, -2, "GetAlpha");

  lua_pushcfunction(L, LuaScriptObject_SetParent);
  lua_setfield(L, -2, "SetParent");

  lua_pushcfunction(L, LuaScriptObject_GetParent);
  lua_setfield(L, -2, "GetParent");

  lua_pushcfunction(L, LuaFrame_SetFrameStrata);
  lua_setfield(L, -2, "SetFrameStrata");

  lua_pushcfunction(L, LuaFrame_GetFrameStrata);
  lua_setfield(L, -2, "GetFrameStrata");

  lua_pushcfunction(L, LuaFrame_SetFrameLevel);
  lua_setfield(L, -2, "SetFrameLevel");

  lua_pushcfunction(L, LuaFrame_GetFrameLevel);
  lua_setfield(L, -2, "GetFrameLevel");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_enableMouse");
      NotifyFrameInputMutation(Ls, 1, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "EnableMouse");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_enableKeyboard");
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "EnableKeyboard");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_movable");
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "SetMovable");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_resizable");
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "SetResizable");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushinteger(Ls, luaL_optinteger(Ls, 2, 0));
      lua_setfield(Ls, 1, "__ow_id");
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "SetID");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_getfield(Ls, 1, "__ow_id");
      if (lua_isinteger(Ls, -1))
        return 1;
      lua_pop(Ls, 1);
    }
    lua_pushinteger(Ls, 0);
    return 1;
  }, 0);
  lua_setfield(L, -2, "GetID");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameObjectSelf(Ls, "Frame");
    if (lua_type(Ls, 2) != LUA_TBOOLEAN) {
      return luaL_error(Ls, "Usage: %s:IgnoreDepth(ignore)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }
    if (lua_istable(Ls, self)) {
      const bool ignore = lua_toboolean(Ls, 2) != 0;
      lua_pushboolean(Ls, ignore);
      lua_setfield(Ls, self, "__ow_ignoreDepth");

      auto *script_obj = static_cast<openwow::ui::widgets::CScriptObject *>(
          openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(Ls, self));
      if (auto *frame = dynamic_cast<openwow::ui::widgets::CSimpleFrame *>(script_obj)) {
        frame->SetIgnoreDepth(ignore);
      }
    }
    return 0;
  }, 0);
  lua_setfield(L, -2, "IgnoreDepth");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (!lua_istable(Ls, 1)) {
      lua_pushnil(Ls);
      return 1;
    }
    lua_getfield(Ls, 1, "__ow_ignoreDepth");
    bool depth = lua_toboolean(Ls, -1) != 0;
    lua_pop(Ls, 1);
    if (depth) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, -2, "IsIgnoringDepth");
}

int LuaFrame_GetAttribute(lua_State *L) {
  const int self_index = ValidateFrameLikeObjectSelf(L);

  if (lua_gettop(L) == 4 && lua_isstring(L, 3)) {
    const char *prefix = lua_tostring(L, 2);
    const char *name = lua_tostring(L, 3);
    const char *suffix = lua_tostring(L, 4);

    const auto exact_key = openwow::ui::NormalizeFrameAttributeKey(
        openwow::ui::BuildTruncatedFrameAttributeKey(
            {prefix != nullptr ? prefix : "", name != nullptr ? name : "",
             suffix != nullptr ? suffix : ""})
            .c_str());
    if (openwow::ui::PushLuaFrameAttributeValue(L, self_index, exact_key)) {
      return 1;
    }

    const auto star_name_suffix_key = openwow::ui::NormalizeFrameAttributeKey(
        openwow::ui::BuildTruncatedFrameAttributeKey(
            {"*", name != nullptr ? name : "", suffix != nullptr ? suffix : ""})
            .c_str());
    if (openwow::ui::PushLuaFrameAttributeValue(
            L, self_index, star_name_suffix_key)) {
      return 1;
    }

    const auto prefix_name_star_key = openwow::ui::NormalizeFrameAttributeKey(
        openwow::ui::BuildTruncatedFrameAttributeKey(
            {prefix != nullptr ? prefix : "", name != nullptr ? name : "", "*"})
            .c_str());
    if (openwow::ui::PushLuaFrameAttributeValue(
            L, self_index, prefix_name_star_key)) {
      return 1;
    }

    const auto star_name_star_key = openwow::ui::NormalizeFrameAttributeKey(
        openwow::ui::BuildTruncatedFrameAttributeKey(
            {"*", name != nullptr ? name : "", "*"})
            .c_str());
    if (openwow::ui::PushLuaFrameAttributeValue(L, self_index, star_name_star_key)) {
      return 1;
    }

    const auto plain_name_key = openwow::ui::NormalizeFrameAttributeKey(name);
    if (openwow::ui::PushLuaFrameAttributeValue(L, self_index, plain_name_key)) {
      return 1;
    }

    lua_pushnil(L);
    return 1;
  }

  if (!lua_isstring(L, 2)) {
    return luaL_error(L, "Usage: %s:GetAttribute(\"name\")",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  const auto key = openwow::ui::NormalizeFrameAttributeKey(lua_tostring(L, 2));
  if (openwow::ui::PushLuaFrameAttributeValue(L, self_index, key)) {
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaFrame_SetAttribute(lua_State *L) {
  const int self_index = ValidateFrameLikeObjectSelf(L);
  if (detail::LuaFrameAttributeMutationBlocked(L, self_index)) {
    return 0;
  }

  const bool has_value_argument = lua_gettop(L) >= 3;
  if (!lua_isstring(L, 2) || !has_value_argument) {
    return luaL_error(L, "Usage: %s:SetAttribute(\"name\", value)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }
  lua_settop(L, 3);

  const auto normalized_key =
      openwow::ui::NormalizeFrameAttributeKey(lua_tostring(L, 2));

  const auto caller_taint = openwow::ui::lua_get_execution_taint_state(L);
  openwow::ui::lua_set_execution_taint_state(L, {});
  openwow::ui::lua_set_taint(L, 3, 0);
  openwow::ui::StoreLuaFrameAttributeValue(L, self_index, normalized_key, 3);

  lua_pushlstring(L, normalized_key.data(), static_cast<size_t>(normalized_key.size()));
  lua_pushvalue(L, 3);
  openwow::ui::lua_set_taint(L, -2, 0);
  openwow::ui::lua_set_taint(L, -1, 0);
  FireScript(L, self_index, "OnAttributeChanged", 2);
  openwow::ui::lua_set_execution_taint_state(L, caller_taint);
  return 0;
}

void ApplyCommonFrameMethods(lua_State *L) {

  int frame = lua_absindex(L, -1);

  lua_newtable(L);
  lua_setfield(L, frame, "__ow_children");
  lua_newtable(L);
  lua_setfield(L, frame, "__ow_regions");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int self_idx = ValidateFrameSelf(Ls);
        const char *requested_fs_name = nullptr;
        if (lua_isstring(Ls, 2) != 0) {
          requested_fs_name = lua_tostring(Ls, 2);
        }
        const std::string resolved_fs_name =
            ExpandLuaParentNameToken(Ls, self_idx, requested_fs_name);
        const char *fs_name = resolved_fs_name.empty() ? nullptr : resolved_fs_name.c_str();
        int draw_layer_id = 2;
        const bool explicit_draw_layer = lua_isstring(Ls, 3) != 0;
        if (explicit_draw_layer) {
          TryParseDrawLayerName(lua_tostring(Ls, 3), &draw_layer_id);
        }
        const char *inherited_or_font = lua_isstring(Ls, 4) ? lua_tostring(Ls, 4) : nullptr;

        bool has_named_font = false;
        TemplateResolveResult inherited_templates;
        if (inherited_or_font != nullptr && inherited_or_font[0] != '\0') {
          has_named_font = PushNamedFontObject(Ls, inherited_or_font);
          if (!has_named_font) {
            lua_pop(Ls, 1);
            inherited_templates = ResolveTemplateNodes(Ls, inherited_or_font, "FontString");
            if (!inherited_templates.ok()) {
              return RaiseInheritedTemplateError(Ls, 1, "CreateFontString", inherited_templates);
            }
          }
        }

        CreateFontStringTable(Ls, self_idx);

        lua_pushstring(Ls, GetDrawLayerNameById(draw_layer_id));
        lua_setfield(Ls, -2, "__ow_draw_layer");
        if (fs_name != nullptr) {
          lua_pushstring(Ls, fs_name);
          lua_setfield(Ls, -2, "__ow_name");
          (void)openwow::ui::PublishLuaGlobalValueIfNil(Ls, fs_name, -1);
        }

        if (has_named_font) {
          SetBoundFontObject(Ls, -1, -2);
          CopyNamedFontObjectStyle(Ls, -1, -2);
          lua_remove(Ls, -2);
        } else {
          ApplyResolvedFontStringTemplates(Ls, self_idx, -1, inherited_templates,
                                           explicit_draw_layer);
        }
        if (explicit_draw_layer) {
          lua_pushstring(Ls, GetDrawLayerNameById(draw_layer_id));
          lua_setfield(Ls, -2, "__ow_draw_layer");
        }
        SyncRegionDrawLayerEnabled(Ls, -1);
        TrackRuntimeRegion(Ls, self_idx, -1, "FontString", fs_name,
                           GetDrawLayerNameById(draw_layer_id));
        return 1;
      },
      0);
  lua_setfield(L, frame, "CreateFontString");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        if (!lua_istable(Ls, 1)) {
          lua_pushnil(Ls);
          return 1;
        }
        const char *requested_tx_name = nullptr;
        if (lua_isstring(Ls, 2) != 0) {
          requested_tx_name = lua_tostring(Ls, 2);
        }
        const std::string resolved_tx_name = ExpandLuaParentNameToken(Ls, 1, requested_tx_name);
        const char *tx_name = resolved_tx_name.empty() ? nullptr : resolved_tx_name.c_str();

        int draw_layer_id = 2;
        const bool explicit_draw_layer = lua_isstring(Ls, 3) != 0;
        if (explicit_draw_layer) {
          TryParseDrawLayerName(lua_tostring(Ls, 3), &draw_layer_id);
        }
        const char *inherited = lua_isstring(Ls, 4) ? lua_tostring(Ls, 4) : nullptr;
        TemplateResolveResult inherited_templates;
        if (inherited != nullptr && inherited[0] != '\0') {
          inherited_templates = ResolveTemplateNodes(Ls, inherited, "Texture");
          if (!inherited_templates.ok()) {
            return RaiseInheritedTemplateError(Ls, 1, "CreateTexture", inherited_templates);
          }
        }

        CreateTextureTable(Ls, 1);
        lua_pushinteger(Ls, static_cast<lua_Integer>(draw_layer_id));
        lua_setfield(Ls, -2, "__ow_draw_layer");
        if (tx_name != nullptr) {
          lua_pushstring(Ls, tx_name);
          lua_setfield(Ls, -2, "__ow_name");
          (void)openwow::ui::PublishLuaGlobalValueIfNil(Ls, tx_name, -1);
        }
        ApplyResolvedTextureTemplates(Ls, -1, inherited_templates, explicit_draw_layer);
        if (explicit_draw_layer) {
          lua_pushinteger(Ls, static_cast<lua_Integer>(draw_layer_id));
          lua_setfield(Ls, -2, "__ow_draw_layer");
        }
        SyncRegionDrawLayerEnabled(Ls, -1);
        TrackRuntimeRegion(Ls, 1, -1, "Texture", tx_name, GetDrawLayerNameById(draw_layer_id));
        return 1;
      },
      0);
  lua_setfield(L, frame, "CreateTexture");

  lua_pushcclosure(
      L,
      [](lua_State *Ls) -> int {
        const int self_index = ValidateFrameLikeObjectSelf(Ls);
        if (lua_isnil(Ls, 2) != 0) {
          ClearLuaBackdropShadow(Ls, self_index);
          if (auto *frame_object = GetLuaBackdropFrame(Ls, self_index); frame_object != nullptr) {
            frame_object->ClearBackdrop();
          }

          ClearRuntimeBackdropPieces(Ls, self_index);
          NotifyFrameInputMutation(Ls, self_index, true);
          return 0;
        }

        if (lua_istable(Ls, 2) == 0) {
          return luaL_error(Ls,
                            "Usage: %s:SetBackdrop(nil or {bgFile = \"bgFile\", edgeFile = "
                            "\"edgeFile\", tile = false, tileSize = 0, edgeSize = 32, insets = { "
                            "left = 0, right = 0, top = 0, bottom = 0 }})",
                            lua_adapter::ScriptObjectDisplayName(Ls, self_index));
        }

        const auto backdrop = ReadLuaBackdropInfo(Ls, 2);
        StoreLuaBackdropShadow(Ls, self_index, backdrop);
        StoreLuaBackdropColorShadow(Ls, self_index, kLuaBackdropColorRField,
                                    kLuaBackdropColorGField, kLuaBackdropColorBField,
                                    kLuaBackdropColorAField, 1.0f, 1.0f, 1.0f, 1.0f);
        StoreLuaBackdropColorShadow(Ls, self_index, kLuaBackdropBorderColorRField,
                                    kLuaBackdropBorderColorGField, kLuaBackdropBorderColorBField,
                                    kLuaBackdropBorderColorAField, 1.0f, 1.0f, 1.0f, 1.0f);

        if (auto *frame_object = GetLuaBackdropFrame(Ls, self_index); frame_object != nullptr) {
          frame_object->SetBackdrop(backdrop);
          frame_object->SetBackdropColor(1.0f, 1.0f, 1.0f, 1.0f);
          frame_object->SetBackdropBorderColor(1.0f, 1.0f, 1.0f, 1.0f);
        }

        RebuildRuntimeBackdropPieces(Ls, self_index);
        NotifyFrameInputMutation(Ls, self_index, true);
        return 0;
      },
      0);
  lua_setfield(L, frame, "SetBackdrop");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    auto *frame_object = GetLuaBackdropFrame(Ls, self_index);
    const bool has_backdrop = (frame_object != nullptr && frame_object->HasBackdrop()) ||
                              HasLuaBackdropShadow(Ls, self_index);
    if (!has_backdrop) {
      return 0;
    }

    const float red = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 2, 0.0));
    const float green = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 3, 0.0));
    const float blue = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 4, 0.0));
    const float alpha = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 5, 1.0));
    StoreLuaBackdropColorShadow(Ls, self_index, kLuaBackdropColorRField,
                                kLuaBackdropColorGField, kLuaBackdropColorBField,
                                kLuaBackdropColorAField, red, green, blue, alpha);
    if (frame_object != nullptr && frame_object->HasBackdrop()) {
      frame_object->SetBackdropColor(red, green, blue, alpha);
    }
    ApplyRuntimeBackdropPieceColors(Ls, self_index, false, red,
                                    green, blue, alpha);
    NotifyFrameInputMutation(Ls, self_index, true);
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetBackdropColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    auto *frame_object = GetLuaBackdropFrame(Ls, self_index);
    const bool has_backdrop = (frame_object != nullptr && frame_object->HasBackdrop()) ||
                              HasLuaBackdropShadow(Ls, self_index);
    if (!has_backdrop) {
      return 0;
    }

    const float red = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 2, 0.0));
    const float green = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 3, 0.0));
    const float blue = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 4, 0.0));
    const float alpha = NormalizeBackdropLuaColorComponent(luaL_optnumber(Ls, 5, 1.0));
    StoreLuaBackdropColorShadow(
        Ls, self_index, kLuaBackdropBorderColorRField,
        kLuaBackdropBorderColorGField, kLuaBackdropBorderColorBField,
        kLuaBackdropBorderColorAField, red, green, blue, alpha);
    if (frame_object != nullptr && frame_object->HasBackdrop()) {
      frame_object->SetBackdropBorderColor(red, green, blue, alpha);
    }
    ApplyRuntimeBackdropPieceColors(Ls, self_index, true, red, green,
                                    blue, alpha);
    NotifyFrameInputMutation(Ls, self_index, true);
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetBackdropBorderColor");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameSelf(Ls);
    lua_getfield(Ls, self, "__ow_children");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      return 0;
    }
    lua_Integer n = luaL_len(Ls, -1);
    const int result_count = openwow::ui::ReserveLuaResultCapacity(
        Ls, static_cast<std::size_t>(n), "frame children");
    for (lua_Integer i = 1; i <= n; ++i) {
      lua_geti(Ls, -static_cast<int>(i), i);
    }
    lua_remove(Ls, -static_cast<int>(n) - 1);
    return result_count;
  }, 0);
  lua_setfield(L, frame, "GetChildren");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameSelf(Ls);
    lua_getfield(Ls, self, "__ow_children");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushinteger(Ls, 0);
      return 1;
    }
    lua_pushinteger(Ls, luaL_len(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetNumChildren");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameSelf(Ls);
    lua_getfield(Ls, self_idx, "__ow_regions");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      return 0;
    }
    int arr = lua_absindex(Ls, -1);
    lua_Integer n = luaL_len(Ls, arr);
    const int result_count = openwow::ui::ReserveLuaResultCapacity(
        Ls, static_cast<std::size_t>(n), "frame regions");
    for (lua_Integer i = 1; i <= n; ++i) {
      lua_geti(Ls, arr, i);
    }
    lua_remove(Ls, arr);
    return result_count;
  }, 0);
  lua_setfield(L, frame, "GetRegions");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameSelf(Ls);
    lua_getfield(Ls, self_idx, "__ow_regions");
    if (!lua_istable(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushinteger(Ls, 0);
      return 1;
    }
    lua_pushinteger(Ls, luaL_len(Ls, -1));
    lua_remove(Ls, -2);
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetNumRegions");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameSelf(Ls);
    if (detail::LuaFrameMutationBlocked(Ls, self)) {
      return 0;
    }
    lua_pushinteger(Ls, static_cast<lua_Integer>(luaL_optinteger(Ls, 2, 0)));
    lua_setfield(Ls, self, "__ow_id");
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetID");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameSelf(Ls);
    lua_getfield(Ls, self, "__ow_id");
    if (!lua_isinteger(Ls, -1)) {
      lua_pop(Ls, 1);
      lua_pushinteger(Ls, 0);
    }
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetID");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    if (lua_istable(Ls, 1)) {
      lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, 1, "__ow_clamped");
      NotifyFrameInputMutation(Ls, 1, false);
    }
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetClampedToScreen");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (!detail::AllowLuaFrameProtectedMutation(Ls, self_idx)) {
      return 0;
    }
    lua_pushboolean(Ls, detail::ScriptReadBoolArgOrDefault(Ls, 2, true));
    lua_setfield(Ls, self_idx, "__ow_toplevel");
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetToplevel");

  lua_pushcfunction(L, LuaFrame_HasScript);
  lua_setfield(L, frame, "HasScript");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    if (lua_isstring(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:IsObjectType(\"type\")",
                        lua_adapter::ScriptObjectDisplayName(Ls, self));
    }

    const char *query = lua_tostring(Ls, 2);
    if (LuaFrameMatchesObjectType(GetLuaFrameRuntimeTypeName(Ls, self), query)) {
      lua_pushnumber(Ls, 1);
    } else {
      lua_pushnil(Ls);
    }
    return 1;
  }, 0);
  lua_setfield(L, frame, "IsObjectType");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    lua_pushnumber(Ls, ComputeFrameEffectiveScale(Ls, self_index));
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetEffectiveScale");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    lua_pushnumber(Ls, ReadFrameScaleFieldOrDefault(Ls, self_index));
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetScale");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    if (detail::LuaFrameMutationBlocked(Ls, self_index)) {
      return 0;
    }

    const char* usage_name = lua_adapter::ScriptObjectDisplayName(Ls, self_index);
    if (lua_isnumber(Ls, 2) == 0) {
      return luaL_error(Ls, "Usage: %s:SetScale(scale)", usage_name);
    }

    const lua_Number scale = lua_tonumber(Ls, 2);
    if (scale <= 0.0) {
      return luaL_error(Ls, "%s:SetScale(): Scale must be > 0", usage_name);
    }

    StoreLuaFrameScaleAndInvalidate(Ls, self_index, static_cast<float>(scale));
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetScale");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    std::uint32_t button_mask = 0;
    if (lua_isstring(Ls, 2) != 0) {
      for (int arg = 2; lua_isstring(Ls, arg) != 0; ++arg) {
        const char *button_name = lua_tostring(Ls, arg);
        button_mask |= openwow::ui::widgets::MouseButtonFlag(button_name);
      }
    }
    lua_pushinteger(Ls, static_cast<lua_Integer>(button_mask));
    lua_setfield(Ls, self_idx, "__ow_registered_drag_button_mask");
    return 0;
  }, 0);
  lua_setfield(L, frame, "RegisterForDrag");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (detail::LuaFrameMutationBlocked(Ls, self_idx)) {
      return 0;
    }

    const char* usage_name = lua_adapter::ScriptObjectDisplayName(Ls, self_idx);
    if (!openwow::ui::ReadLuaBooleanFieldOrDefault(Ls, self_idx, "__ow_movable", false)) {
      return luaL_error(Ls, "Frame %s is not movable", usage_name);
    }

    auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls);
    const char *frame_key = GetFrameRuntimeKeyOrName(Ls, self_idx);
    if (manager != nullptr && frame_key != nullptr &&
        manager->input_router().BeginFrameMoveSizing(frame_key, 4)) {
      lua_pushboolean(Ls, 1);
      lua_setfield(Ls, self_idx, "__ow_user_placed");
      manager->frame_store().SetFrameUserPlaced(frame_key, true);
    }
    return 0;
  }, 0);
  lua_setfield(L, frame, "StartMoving");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    const char *frame_key = GetFrameRuntimeKeyOrName(Ls, self_idx);
    if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls); manager != nullptr) {
      if (frame_key != nullptr) {
        (void)manager->input_router().StopFrameMoveSizing(frame_key);
      }
    }
    return 0;
  }, 0);
  lua_setfield(L, frame, "StopMovingOrSizing");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_index = ValidateFrameLikeObjectSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0 ||
        lua_isnumber(Ls, 4) == 0 || lua_isnumber(Ls, 5) == 0) {
      return luaL_error(Ls, "Usage: %s:SetClampRectInsets(left, right, top, bottom)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_index));
    }

    const lua_Number left = lua_tonumber(Ls, 2);
    const lua_Number right = lua_tonumber(Ls, 3);
    const lua_Number top = lua_tonumber(Ls, 4);
    const lua_Number bottom = lua_tonumber(Ls, 5);

    openwow::ui::WriteLuaNumberField(Ls, self_index, "__ow_clamp_l", left);
    openwow::ui::WriteLuaNumberField(Ls, self_index, "__ow_clamp_r", right);
    openwow::ui::WriteLuaNumberField(Ls, self_index, "__ow_clamp_t", top);
    openwow::ui::WriteLuaNumberField(Ls, self_index, "__ow_clamp_b", bottom);
    NotifyFrameInputMutation(Ls, self_index, false);

    auto *script_object = static_cast<openwow::ui::widgets::CScriptObject *>(
        openwow::ui::game::detail::GetLuaNativeScriptObjectThisPointer(Ls, self_index));
    auto *frame_object = dynamic_cast<openwow::ui::widgets::CSimpleFrame *>(script_object);
    if (frame_object != nullptr) {
      frame_object->SetClampRectInsets(
          openwow::ui::PixelUiHorizontalCoordinateToStored(static_cast<float>(left)),
          openwow::ui::PixelUiHorizontalCoordinateToStored(static_cast<float>(bottom)),
          openwow::ui::PixelUiHorizontalCoordinateToStored(static_cast<float>(right)),
          openwow::ui::PixelUiHorizontalCoordinateToStored(static_cast<float>(top)));
    }
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetClampRectInsets");

  lua_pushcfunction(L, LuaFrame_Raise);
  lua_setfield(L, frame, "Raise");
  lua_pushcfunction(L, LuaFrame_Lower);
  lua_setfield(L, frame, "Lower");
  lua_pushcclosure(L, [](lua_State * ) -> int { return 0; }, 0);
  lua_setfield(L, frame, "SetUserPlaced");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    lua_pushboolean(Ls, 0);
    return 1;
  }, 0);
  lua_setfield(L, frame, "IsUserPlaced");

  lua_pushcfunction(L, LuaRegion_GetSize);
  lua_setfield(L, frame, "GetSize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      lua_pushnil(Ls);
      lua_pushnil(Ls);
      return 2;
    }

    lua_pushnumber(Ls, rect.center_x());
    lua_pushnumber(Ls, rect.center_y());
    return 2;
  }, 0);
  lua_setfield(L, frame, "GetCenter");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.left);
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetLeft");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.right());
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetRight");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.top());
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetTop");
  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      lua_pushnil(Ls);
      return 1;
    }

    lua_pushnumber(Ls, rect.bottom);
    return 1;
  }, 0);
  lua_setfield(L, frame, "GetBottom");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameScriptSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameRect(Ls, self, &rect)) {
      return 0;
    }

    lua_pushnumber(Ls, rect.left);
    lua_pushnumber(Ls, rect.bottom);
    lua_pushnumber(Ls, rect.width);
    lua_pushnumber(Ls, rect.height);
    return 4;
  }, 0);
  lua_setfield(L, frame, "GetRect");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self = ValidateFrameLikeObjectSelf(Ls);
    ScriptFrameUiRect rect{};
    if (!TryGetScriptFrameBoundsRect(Ls, self, &rect)) {
      return 0;
    }

    lua_pushnumber(Ls, rect.left);
    lua_pushnumber(Ls, rect.bottom);
    lua_pushnumber(Ls, rect.width);
    lua_pushnumber(Ls, rect.height);
    return 4;
  }, 0);
  lua_setfield(L, frame, "GetBoundsRect");

  lua_pushcfunction(L, LuaRegion_IsDragging);
  lua_setfield(L, frame, "IsDragging");

  lua_pushcfunction(L, LuaFrame_SetAttribute);
  lua_setfield(L, frame, "SetAttribute");

  lua_pushcfunction(L, LuaFrame_GetAttribute);
  lua_setfield(L, frame, "GetAttribute");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(Ls, "Usage: %s:SetMaxResize(maxWidth, maxHeight)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
    }
    const float width_pixels = static_cast<float>(lua_tonumber(Ls, 2));
    const float height_pixels = static_cast<float>(lua_tonumber(Ls, 3));
    lua_pushnumber(Ls, static_cast<lua_Number>(
        openwow::ui::anim::PixelAnimationOffsetToStored(width_pixels)));
    lua_setfield(Ls, self_idx, "__ow_max_w");
    lua_pushnumber(Ls, static_cast<lua_Number>(
        openwow::ui::anim::PixelAnimationOffsetToStored(height_pixels)));
    lua_setfield(Ls, self_idx, "__ow_max_h");
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetMaxResize");

  lua_pushcclosure(L, [](lua_State *Ls) -> int {
    const int self_idx = ValidateFrameResizeSelf(Ls);
    if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0) {
      return luaL_error(Ls, "Usage: %s:SetMinResize(minWidth, minHeight)",
                        lua_adapter::ScriptObjectDisplayName(Ls, self_idx));
    }
    const float width_pixels = static_cast<float>(lua_tonumber(Ls, 2));
    const float height_pixels = static_cast<float>(lua_tonumber(Ls, 3));
    lua_pushnumber(Ls, static_cast<lua_Number>(
        openwow::ui::anim::PixelAnimationOffsetToStored(width_pixels)));
    lua_setfield(Ls, self_idx, "__ow_min_w");
    lua_pushnumber(Ls, static_cast<lua_Number>(
        openwow::ui::anim::PixelAnimationOffsetToStored(height_pixels)));
    lua_setfield(Ls, self_idx, "__ow_min_h");
    return 0;
  }, 0);
  lua_setfield(L, frame, "SetMinResize");

  ApplyFrameStateMethods(L);
}

}

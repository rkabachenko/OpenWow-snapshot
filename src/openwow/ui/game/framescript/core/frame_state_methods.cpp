#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_base_methods.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/game/framescript/core/frame_backdrop_runtime.h"
#include "openwow/ui/game/framescript/core/frame_draw_layer_state.h"
#include "openwow/ui/game/framescript/core/frame_event_methods.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_layout_state.h"
#include "openwow/ui/game/framescript/core/frame_lua_receiver.h"
#include "openwow/ui/game/framescript/core/frame_region_state.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include <lua.hpp>

#include <cstdint>
#include <string>

#undef lua_pushcfunction
#define lua_pushcfunction(L, ...) lua_pushcclosure(L, (__VA_ARGS__), 0)

namespace openwow::ui::game::frame_api {

namespace {

constexpr const char* kFrameMaxResizeWidthField = "__ow_max_w";
constexpr const char* kFrameMaxResizeHeightField = "__ow_max_h";
constexpr const char* kFrameMinResizeWidthField = "__ow_min_w";
constexpr const char* kFrameMinResizeHeightField = "__ow_min_h";
constexpr const char* kFrameRegisteredDragButtonMaskField =
    "__ow_registered_drag_button_mask";

}

using detail::TruncateLuaNumberToSseI32;
using detail::FindInterleavedQuestIndexById;
using detail::FrameScript_PushNumberFromInt;
using detail::ScriptReadBoolArgOrDefault;
using detail::lua_pushwowbool;
using detail::FrameScript_PushBoolean;
using detail::GetWorldSession;
using detail::GetDbcLoader;
using detail::AllowLuaFrameProtectedMutation;
using detail::LuaFrameAttributeMutationBlocked;
using detail::GetLuaWidgetShownState;
using detail::SafeLuaString;
using detail::ResolveSpellBookSpellId;
using detail::LuaFrameCanChangeProtectedState;
using detail::PushLuaLayoutProtectionResults;
using detail::LuaFrameMutationBlocked;

void InstallFrameBackdropMethods(lua_State* L) {
  const int frame = lua_absindex(L, -1);

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);

      openwow::ui::widgets::BackdropInfo backdrop;
      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx);
          frame_object != nullptr && frame_object->HasBackdrop()) {
        backdrop = frame_object->GetBackdrop();
      } else if (!TryReadLuaBackdropShadow(Ls, self_idx, &backdrop)) {
        return 0;
      }

      const int result_index = EnsureBackdropResultTable(Ls);
      FillBackdropResultTable(Ls, result_index, backdrop);
      return 1;
    });
    lua_setfield(L, frame, "GetBackdrop");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx);
          frame_object != nullptr && frame_object->HasBackdrop()) {
        float red = 1.0f;
        float green = 1.0f;
        float blue = 1.0f;
        float alpha = 1.0f;
        frame_object->GetBackdropColor(red, green, blue, alpha);
        lua_pushnumber(Ls, red);
        lua_pushnumber(Ls, green);
        lua_pushnumber(Ls, blue);
        lua_pushnumber(Ls, alpha);
        return 4;
      }

      if (!HasLuaBackdropShadow(Ls, self_idx)) {
        return 0;
      }

      PushLuaBackdropColorFieldOrDefault(Ls, self_idx, kLuaBackdropColorRField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(Ls, self_idx, kLuaBackdropColorGField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(Ls, self_idx, kLuaBackdropColorBField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(Ls, self_idx, kLuaBackdropColorAField, 1.0f);
      return 4;
    });
    lua_setfield(L, frame, "GetBackdropColor");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx);
          frame_object != nullptr && frame_object->HasBackdrop()) {
        float red = 1.0f;
        float green = 1.0f;
        float blue = 1.0f;
        float alpha = 1.0f;
        frame_object->GetBackdropBorderColor(red, green, blue, alpha);
        lua_pushnumber(Ls, red);
        lua_pushnumber(Ls, green);
        lua_pushnumber(Ls, blue);
        lua_pushnumber(Ls, alpha);
        return 4;
      }

      if (!HasLuaBackdropShadow(Ls, self_idx)) {
        return 0;
      }

      PushLuaBackdropColorFieldOrDefault(
          Ls, self_idx, kLuaBackdropBorderColorRField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(
          Ls, self_idx, kLuaBackdropBorderColorGField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(
          Ls, self_idx, kLuaBackdropBorderColorBField, 1.0f);
      PushLuaBackdropColorFieldOrDefault(
          Ls, self_idx, kLuaBackdropBorderColorAField, 1.0f);
      return 4;
    });
    lua_setfield(L, frame, "GetBackdropBorderColor");
  }

void InstallFrameInteractionMethods(lua_State* L) {
  const int frame = lua_absindex(L, -1);

    lua_pushcfunction(L, LuaScriptObject_SetParent);
    lua_setfield(L, frame, "SetParent");

    lua_pushcfunction(L, LuaScriptObject_GetParent);
    lua_setfield(L, frame, "GetParent");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx); frame_object != nullptr) {
        float left = 0.0f;
        float bottom = 0.0f;
        float right = 0.0f;
        float top = 0.0f;
        frame_object->GetClampRectInsets(left, right, top, bottom);
        lua_pushnumber(
            Ls, static_cast<lua_Number>(openwow::ui::StoredUiHorizontalCoordinateToPixels(left)));
        lua_pushnumber(
            Ls, static_cast<lua_Number>(openwow::ui::StoredUiHorizontalCoordinateToPixels(right)));
        lua_pushnumber(
            Ls, static_cast<lua_Number>(openwow::ui::StoredUiHorizontalCoordinateToPixels(top)));
        lua_pushnumber(
            Ls, static_cast<lua_Number>(openwow::ui::StoredUiHorizontalCoordinateToPixels(bottom)));
        return 4;
      }

      lua_getfield(Ls, self_idx, "__ow_clamp_l");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, self_idx, "__ow_clamp_r");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, self_idx, "__ow_clamp_t");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      lua_getfield(Ls, self_idx, "__ow_clamp_b");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      return 4;
    });
    lua_setfield(L, frame, "GetClampRectInsets");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, self_idx, "__ow_movable");
      return 0;
    });
    lua_setfield(L, frame, "SetMovable");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_getfield(Ls, self_idx, "__ow_movable");
      const bool movable = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      lua_pushwowbool(Ls, movable);
      return 1;
    });
    lua_setfield(L, frame, "IsMovable");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, self_idx, "__ow_resizable");
      return 0;
    });
    lua_setfield(L, frame, "SetResizable");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_getfield(Ls, self_idx, "__ow_resizable");
      const bool resizable = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      lua_pushwowbool(Ls, resizable);
      return 1;
    });
    lua_setfield(L, frame, "IsResizable");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (lua_istable(Ls, 1)) {
        lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
        lua_setfield(Ls, 1, "__ow_clamped");
        NotifyFrameInputMutation(Ls, 1, false);
      }
      return 0;
    });
    lua_setfield(L, frame, "SetClampedToScreen");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (!lua_istable(Ls, 1)) {
        lua_pushboolean(Ls, 0);
        return 1;
      }
      lua_getfield(Ls, 1, "__ow_clamped");
      lua_pushboolean(Ls, lua_toboolean(Ls, -1));
      lua_remove(Ls, -2);
      return 1;
    });
    lua_setfield(L, frame, "IsClampedToScreen");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (!AllowLuaFrameProtectedMutation(Ls, self_idx)) {
        return 0;
      }

      if (lua_isnumber(Ls, 2) == 0 || lua_isnumber(Ls, 3) == 0 ||
          lua_isnumber(Ls, 4) == 0 || lua_isnumber(Ls, 5) == 0) {
        return luaL_error(Ls, "Usage: %s:SetHitRectInsets(left, right, top, bottom)",
                          GetFrameUsageObjectName(Ls, self_idx).c_str());
      }

      const lua_Number left = lua_tonumber(Ls, 2);
      const lua_Number right = lua_tonumber(Ls, 3);
      const lua_Number top = lua_tonumber(Ls, 4);
      const lua_Number bottom = lua_tonumber(Ls, 5);

      SetLuaHitRectInsetField(Ls, self_idx, "__ow_hit_l", left);
      SetLuaHitRectInsetField(Ls, self_idx, "__ow_hit_r", right);
      SetLuaHitRectInsetField(Ls, self_idx, "__ow_hit_t", top);
      SetLuaHitRectInsetField(Ls, self_idx, "__ow_hit_b", bottom);
      NotifyFrameInputMutation(Ls, self_idx, true);

      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx); frame_object != nullptr) {
        frame_object->SetHitRectInsets(static_cast<float>(left), static_cast<float>(right),
                                       static_cast<float>(top), static_cast<float>(bottom));
      }
      return 0;
    });
    lua_setfield(L, frame, "SetHitRectInsets");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (auto *frame_object = GetLuaSimpleFrame(Ls, self_idx); frame_object != nullptr) {
        float left = 0.0f;
        float right = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        frame_object->GetHitRectInsetsOut(&left, &right, &top, &bottom);
        lua_pushnumber(Ls, static_cast<lua_Number>(left));
        lua_pushnumber(Ls, static_cast<lua_Number>(right));
        lua_pushnumber(Ls, static_cast<lua_Number>(top));
        lua_pushnumber(Ls, static_cast<lua_Number>(bottom));
        return 4;
      }

      lua_pushnumber(Ls, ReadLuaHitRectInsetField(Ls, self_idx, "__ow_hit_l"));
      lua_pushnumber(Ls, ReadLuaHitRectInsetField(Ls, self_idx, "__ow_hit_r"));
      lua_pushnumber(Ls, ReadLuaHitRectInsetField(Ls, self_idx, "__ow_hit_t"));
      lua_pushnumber(Ls, ReadLuaHitRectInsetField(Ls, self_idx, "__ow_hit_b"));
      return 4;
    });
    lua_setfield(L, frame, "GetHitRectInsets");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameInputCategoryEnabled(Ls, "__ow_mouse_enabled");
    });
    lua_setfield(L, frame, "EnableMouse");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushFrameInputCategoryEnabled(Ls, "__ow_mouse_enabled", "__ow_enableMouse");
    });
    lua_setfield(L, frame, "IsMouseEnabled");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameInputCategoryEnabled(Ls, "__ow_mousewheel_enabled");
    });
    lua_setfield(L, frame, "EnableMouseWheel");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushFrameInputCategoryEnabled(Ls, "__ow_mousewheel_enabled");
    });
    lua_setfield(L, frame, "IsMouseWheelEnabled");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameInputCategoryEnabled(Ls, "__ow_keyboard_enabled");
    });
    lua_setfield(L, frame, "EnableKeyboard");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushFrameInputCategoryEnabled(Ls, "__ow_keyboard_enabled", "__ow_enableKeyboard");
    });
    lua_setfield(L, frame, "IsKeyboardEnabled");
  }

void InstallFramePresentationMethods(lua_State* L) {
  const int frame = lua_absindex(L, -1);

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      ValidateFrameSelf(Ls);

      std::uint8_t effective_alpha = 0xFF;
      lua_pushvalue(Ls, 1);
      constexpr int kMaxDepth = 64;
      for (int depth = 0; depth < kMaxDepth && lua_istable(Ls, -1) != 0; ++depth) {
        effective_alpha = openwow::ui::game::MultiplyFrameAlphaBytes(
            effective_alpha, GetFrameAlphaByteOrDefault(Ls, -1));
        if (effective_alpha == 0) {
          lua_pop(Ls, 1);
          lua_pushnumber(Ls, 0.0);
          return 1;
        }

        lua_getfield(Ls, -1, "__ow_parent");
        lua_remove(Ls, -2);
      }

      lua_pop(Ls, 1);
      lua_pushnumber(Ls, openwow::ui::game::NormalizeFrameAlphaByte(effective_alpha));
      return 1;
    });
    lua_setfield(L, frame, "GetEffectiveAlpha");

    lua_pushcfunction(L, LuaFrame_SetFrameLevel);
    lua_setfield(L, frame, "SetFrameLevel");

    lua_pushcfunction(L, LuaFrame_GetFrameLevel);
    lua_setfield(L, frame, "GetFrameLevel");

    lua_pushcfunction(L, LuaFrame_SetFrameStrata);
    lua_setfield(L, frame, "SetFrameStrata");

    lua_pushcfunction(L, LuaFrame_GetFrameStrata);
    lua_setfield(L, frame, "GetFrameStrata");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);

      lua_getfield(Ls, self_idx, "__ow_movable");
      const bool movable = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);

      lua_getfield(Ls, self_idx, "__ow_resizable");
      const bool resizable = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);

      if (!movable && !resizable) {
        const std::string frame_name = GetFrameUsageObjectName(Ls, self_idx);
        return luaL_error(Ls, "Frame %s is not movable or resizable", frame_name.c_str());
      }

      const bool user_placed = ScriptReadBoolArgOrDefault(Ls, 2, true);
      lua_pushboolean(Ls, user_placed);
      lua_setfield(Ls, self_idx, "__ow_user_placed");
      SyncTrackedFrameUserPlaced(Ls, self_idx, user_placed);
      return 0;
    });
    lua_setfield(L, frame, "SetUserPlaced");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_getfield(Ls, self_idx, "__ow_user_placed");
      const bool user_placed = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      lua_pushwowbool(Ls, user_placed);
      return 1;
    });
    lua_setfield(L, frame, "IsUserPlaced");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameResizeBounds(Ls, kFrameMaxResizeWidthField, kFrameMaxResizeHeightField,
                                  "Usage: %s:SetMaxResize(maxWidth, maxHeight)");
    });
    lua_setfield(L, frame, "SetMaxResize");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameResizeBounds(Ls, kFrameMinResizeWidthField, kFrameMinResizeHeightField,
                                  "Usage: %s:SetMinResize(minWidth, minHeight)");
    });
    lua_setfield(L, frame, "SetMinResize");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetValidatedFrameShownState(Ls, true);
    });
    lua_setfield(L, frame, "Show");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetValidatedFrameShownState(Ls, false);
    });
    lua_setfield(L, frame, "Hide");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushValidatedFrameShownState(Ls);
    });
    lua_setfield(L, frame, "IsShown");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameSelf(Ls);
      lua_pushwowbool(Ls, IsLuaTableEffectivelyVisible(Ls, self_idx));
      return 1;
    });
    lua_setfield(L, frame, "IsVisible");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (!AllowLuaFrameProtectedMutation(Ls, self_idx)) {
        return 0;
      }
      lua_pushboolean(Ls, ScriptReadBoolArgOrDefault(Ls, 2, true));
      lua_setfield(Ls, self_idx, "__ow_toplevel");
      return 0;
    });
    lua_setfield(L, frame, "SetToplevel");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_getfield(Ls, self_idx, "__ow_toplevel");
      const bool toplevel = lua_toboolean(Ls, -1) != 0;
      lua_pop(Ls, 1);
      lua_pushwowbool(Ls, toplevel);
      return 1;
    });
    lua_setfield(L, frame, "IsToplevel");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (lua_isnumber(Ls, 2) == 0) {
        return luaL_error(Ls, "Usage: %s:SetDepth(additiveDepth)",
                          GetFrameUsageObjectName(Ls, self_idx).c_str());
      }
      const float depth = static_cast<float>(lua_tonumber(Ls, 2));
      lua_pushnumber(Ls, static_cast<lua_Number>(depth));
      lua_setfield(Ls, self_idx, "__ow_depth");
      if (auto *frame = GetLuaSimpleFrame(Ls, self_idx); frame != nullptr) {
        frame->SetDepth(depth, false);
      }
      return 0;
    });
    lua_setfield(L, frame, "SetDepth");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (!lua_istable(Ls, 1))
        return 0;
      lua_getfield(Ls, 1, "__ow_ignoreDepth");
      if (lua_toboolean(Ls, -1)) {
        lua_pop(Ls, 1);
        return 0;
      }
      lua_pop(Ls, 1);
      lua_getfield(Ls, 1, "__ow_depth");
      if (!lua_isnumber(Ls, -1)) {
        lua_pop(Ls, 1);
        lua_pushnumber(Ls, 0);
      }
      return 1;
    });
    lua_setfield(L, frame, "GetDepth");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      lua_pushwowbool(Ls, lua_istable(Ls, 1) != 0 && LuaFrameCanChangeProtectedState(Ls, 1));
      return 1;
    });
    lua_setfield(L, frame, "CanChangeProtectedState");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushLuaLayoutProtectionResults(Ls, ValidateFrameResizeSelf(Ls));
    });
    lua_setfield(L, frame, "IsProtected");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      const bool dont_save_position = ScriptReadBoolArgOrDefault(Ls, 2, true);
      lua_pushboolean(Ls, dont_save_position);
      lua_setfield(Ls, self_idx, "__ow_dontsavepos");
      SyncTrackedFrameDontSavePosition(Ls, self_idx, dont_save_position);
      return 0;
    });
    lua_setfield(L, frame, "SetDontSavePosition");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      lua_getfield(Ls, self_idx, "__ow_dontsavepos");
      int v = lua_toboolean(Ls, -1);
      lua_pop(Ls, 1);
      if (v)
        lua_pushnumber(Ls, 1);
      else
        lua_pushnil(Ls);
      return 1;
    });
    lua_setfield(L, frame, "GetDontSavePosition");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (!lua_istable(Ls, 1)) {
        return 0;
      }

      double effective_depth = 0.0;
      lua_pushvalue(Ls, 1);
      constexpr int kMaxDepthParentChain = 64;
      for (int depth = 0; depth < kMaxDepthParentChain && lua_istable(Ls, -1) != 0; ++depth) {
        lua_getfield(Ls, -1, "__ow_ignoreDepth");
        if (lua_toboolean(Ls, -1) != 0) {
          lua_pop(Ls, 2);
          return 0;
        }
        lua_pop(Ls, 1);

        lua_getfield(Ls, -1, "__ow_depth");
        if (lua_isnumber(Ls, -1) != 0) {
          effective_depth += lua_tonumber(Ls, -1);
        }
        lua_pop(Ls, 1);

        lua_getfield(Ls, -1, "__ow_parent");
        lua_remove(Ls, -2);
      }

      lua_pop(Ls, 1);
      lua_pushnumber(Ls, effective_depth);
      return 1;
    });
    lua_setfield(L, frame, "GetEffectiveDepth");
  }

void InstallFrameMovementMethods(lua_State* L) {
  const int frame = lua_absindex(L, -1);

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return GetFrameResizeBounds(Ls, kFrameMaxResizeWidthField, kFrameMaxResizeHeightField);
    });
    lua_setfield(L, frame, "GetMaxResize");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return GetFrameResizeBounds(Ls, kFrameMinResizeWidthField, kFrameMinResizeHeightField);
    });
    lua_setfield(L, frame, "GetMinResize");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return SetFrameInputCategoryEnabled(Ls, "__ow_joystick_enabled");
    });
    lua_setfield(L, frame, "EnableJoystick");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      return PushFrameInputCategoryEnabled(Ls, "__ow_joystick_enabled");
    });
    lua_setfield(L, frame, "IsJoystickEnabled");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      std::uint32_t button_mask = 0;
      if (lua_isstring(Ls, 2) != 0) {
        for (int arg = 2; lua_isstring(Ls, arg) != 0; ++arg) {
          const char *button_name = lua_tostring(Ls, arg);
          button_mask |= openwow::ui::widgets::MouseButtonFlag(button_name);
        }
      }

      lua_pushinteger(Ls, static_cast<lua_Integer>(button_mask));
      lua_setfield(Ls, self_idx, kFrameRegisteredDragButtonMaskField);
      return 0;
    });
    lua_setfield(L, frame, "RegisterForDrag");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (LuaFrameMutationBlocked(Ls, self_idx)) {
        return 0;
      }

      const std::string usage_name = GetFrameUsageObjectName(Ls, self_idx);
      if (!GetLuaBooleanField(Ls, self_idx, "__ow_movable")) {
        return luaL_error(Ls, "Frame %s is not movable", usage_name.c_str());
      }

      (void)BeginTrackedFrameMoveSizing(Ls, self_idx, GetFrameManagerKey(Ls, self_idx), 4);
      return 0;
    });
    lua_setfield(L, frame, "StartMoving");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (auto *manager = runtime::WorldUiRuntimeContext::FromLua(Ls); manager != nullptr) {
        (void)manager->input_router().StopFrameMoveSizing(
            GetFrameManagerKey(Ls, self_idx));
      }
      return 0;
    });
    lua_setfield(L, frame, "StopMovingOrSizing");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameResizeSelf(Ls);
      if (LuaFrameMutationBlocked(Ls, self_idx)) {
        return 0;
      }

      const std::string usage_name = GetFrameUsageObjectName(Ls, self_idx);
      if (!GetLuaBooleanField(Ls, self_idx, "__ow_resizable")) {
        return luaL_error(Ls, "Frame %s is not resizable", usage_name.c_str());
      }

      int mode = 8;
      if (lua_gettop(Ls) >= 2 && lua_isstring(Ls, 2) != 0) {
        const char *point_name = lua_tostring(Ls, 2);
        if (point_name != nullptr) {
          (void)openwow::ui::StringToFramePoint(point_name, &mode);
        }
      }

      (void)BeginTrackedFrameMoveSizing(Ls, self_idx, GetFrameManagerKey(Ls, self_idx), mode);
      return 0;
    });
    lua_setfield(L, frame, "StartSizing");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (!lua_istable(Ls, 1)) {
        lua_pushnil(Ls);
        return 1;
      }
      return PushStoredTitleRegion(Ls, 1);
    });
    lua_setfield(L, frame, "GetTitleRegion");

    lua_pushcfunction(L, LuaRegion_IsMouseOver);
    lua_setfield(L, frame, "IsMouseOver");
  }

void InstallFrameSecurityAndAnimationMethods(lua_State* L) {
  const int frame = lua_absindex(L, -1);

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameSelf(Ls);
      int draw_layer_id = 2;
      if (lua_isstring(Ls, 2) != 0) {
        TryParseDrawLayerName(lua_tostring(Ls, 2), &draw_layer_id);
      }

      const char *canonical_name = GetDrawLayerNameById(draw_layer_id);
      SetFrameDrawLayerEnabled(Ls, self_idx, canonical_name, true);
      SyncFrameRegionsForDrawLayer(Ls, self_idx, canonical_name);
      return 0;
    });
    lua_setfield(L, frame, "EnableDrawLayer");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      const int self_idx = ValidateFrameSelf(Ls);
      int draw_layer_id = 2;
      if (lua_isstring(Ls, 2) != 0) {
        TryParseDrawLayerName(lua_tostring(Ls, 2), &draw_layer_id);
      }

      const char *canonical_name = GetDrawLayerNameById(draw_layer_id);
      SetFrameDrawLayerEnabled(Ls, self_idx, canonical_name, false);
      SyncFrameRegionsForDrawLayer(Ls, self_idx, canonical_name);
      return 0;
    });
    lua_setfield(L, frame, "DisableDrawLayer");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      auto& secure = SecureExecution::Get();
      if (lua_istable(Ls, 1) != 0 &&
          (!secure.InCombatLockdown() || secure.IsSecure(Ls))) {
        lua_pushboolean(Ls, 1);
        lua_setfield(Ls, 1, "__ow_allow_attribute_changes");
      }
      return 0;
    });
    lua_setfield(L, frame, "AllowAttributeChanges");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      lua_pushwowbool(Ls, lua_istable(Ls, 1) != 0 && (!LuaFrameAttributeMutationBlocked(Ls, 1)));
      return 1;
    });
    lua_setfield(L, frame, "CanChangeAttribute");

    lua_pushcfunction(L, LuaFrame_GetAttribute);
    lua_setfield(L, frame, "GetAttribute");

    lua_pushcfunction(L, LuaFrame_SetAttribute);
    lua_setfield(L, frame, "SetAttribute");

    lua_pushcfunction(L, LuaFrame_RegisterAllEvents);
    lua_setfield(L, frame, "RegisterAllEvents");

    lua_pushcfunction(L, LuaFrame_IsEventRegistered);
    lua_setfield(L, frame, "IsEventRegistered");

    lua_pushcfunction(L, [](lua_State *Ls) -> int {
      if (!lua_istable(Ls, 1)) {
        lua_pushnil(Ls);
        return 1;
      }
      return PushOrCreateTitleRegion(Ls, 1);
    });
    lua_setfield(L, frame, "CreateTitleRegion");

    openwow::ui::anim::ApplyAnimationFrameMethods(L);
  }

void ApplyFrameStateMethods(lua_State* lua) {
  InstallFrameBackdropMethods(lua);
  InstallFrameInteractionMethods(lua);
  InstallFramePresentationMethods(lua);
  InstallFrameMovementMethods(lua);
  InstallFrameSecurityAndAnimationMethods(lua);
}

}

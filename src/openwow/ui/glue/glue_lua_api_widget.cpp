#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/font_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml/framexml_value_utils.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/glue/glue_font_metrics.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/ui/glue/widget_lua_bindings.h"

namespace openwow::ui::glue::detail {

void EnsureWidgetMethodTable(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, kWidgetMethodsRegistryKey);
  if (lua_istable(state, -1) != 0) {
    lua_pop(state, 1);
    return;
  }
  lua_pop(state, 1);

  lua_newtable(state);

  struct MethodEntry { const char* name; lua_CFunction fn; };
  static const MethodEntry kMethods[] = {
    {"Show", LuaWidget_Show},
    {"Hide", LuaWidget_Hide},
    {"IsShown", LuaWidget_IsShown},
    {"IsVisible", LuaWidget_IsVisible},
    {"GetObjectType", LuaWidget_GetObjectType},
    {"GetName", LuaWidget_GetName},
    {"GetParent", LuaWidget_GetParent},
    {"SetParent", LuaWidget_SetParent},
    {"SetID", LuaWidget_SetID},
    {"GetID", LuaWidget_GetID},
    {"SetTexture", LuaWidget_SetTexture},
    {"GetTexture", LuaWidget_GetTexture},
    {"SetTexCoord", LuaWidget_SetTexCoord},
    {"SetVertexColor", LuaWidget_SetVertexColor},
    {"SetTextColor", LuaWidget_SetTextColor},
    {"GetTextColor", LuaWidget_GetTextColor},
    {"RegisterEvent", LuaWidget_RegisterEvent},
    {"UnregisterEvent", LuaWidget_UnregisterEvent},
    {"UnregisterAllEvents", LuaWidget_UnregisterAllEvents},
    {"RegisterAllEvents", LuaWidget_RegisterAllEvents},
    {"IsEventRegistered", LuaWidget_IsEventRegistered},
    {"AllowAttributeChanges", LuaWidget_AllowAttributeChanges},
    {"CanChangeAttribute", LuaWidget_CanChangeAttribute},
    {"GetAttribute", LuaWidget_GetAttribute},
    {"SetAttribute", LuaWidget_SetAttribute},
    {"SetPoint", LuaWidget_SetPoint},
    {"SetAllPoints", LuaWidget_SetAllPoints},
    {"ClearAllPoints", LuaWidget_ClearAllPoints},
    {"SetSize", LuaWidget_SetSize},
    {"SetWidth", LuaWidget_SetWidth},
    {"SetHeight", LuaWidget_SetHeight},
    {"GetWidth", LuaWidget_GetWidth},
    {"GetHeight", LuaWidget_GetHeight},
    {"GetRect", LuaWidget_GetRect},
    {"GetLeft", LuaWidget_GetLeft},
    {"GetRight", LuaWidget_GetRight},
    {"GetTop", LuaWidget_GetTop},
    {"GetBottom", LuaWidget_GetBottom},
    {"GetCenter", LuaWidget_GetCenter},
    {"GetNumPoints", LuaWidget_GetNumPoints},
    {"GetPoint", LuaWidget_GetPoint},
    {"SetFrameLevel", LuaWidget_SetFrameLevel},
    {"SetFrameStrata", LuaWidget_SetFrameStrata},
    {"SetAlpha", LuaWidget_SetAlpha},
    {"GetAlpha", LuaWidget_GetAlpha},
    {"SetText", LuaWidget_SetText},
    {"Insert", LuaWidget_Insert},
    {"SetFormattedText", LuaWidget_SetFormattedText},
    {"SetJustifyH", LuaWidget_SetJustifyH},
    {"SetJustifyV", LuaWidget_SetJustifyV},
    {"SetNormalFontObject", LuaWidget_SetNormalFontObject},
    {"SetHighlightFontObject", LuaWidget_SetHighlightFontObject},
    {"SetDisabledFontObject", LuaWidget_SetDisabledFontObject},
    {"GetFontString", LuaWidget_GetFontString},
    {"SetBackdrop", LuaWidget_SetBackdrop},
    {"SetBackdropColor", LuaWidget_SetBackdropColor},
    {"SetBackdropBorderColor", LuaWidget_SetBackdropBorderColor},
    {"GetText", LuaWidget_GetText},
    {"SetEnabled", LuaWidget_SetEnabled},
    {"Enable", LuaWidget_Enable},
    {"Disable", LuaWidget_Disable},
    {"IsEnabled", LuaWidget_IsEnabled},
    {"SetScript", LuaWidget_SetScript},
    {"SetCamera", LuaWidget_SetCamera},
    {"SetSequence", LuaWidget_SetSequence},
    {"SetSequenceTime", LuaWidget_SetSequenceTime},
    {"SetModel", LuaWidget_SetModel},
    {"EnableKeyboard", LuaWidget_EnableKeyboard},
    {"SetAutoFocus", LuaWidget_SetAutoFocus},
    {"IsAutoFocus", LuaWidget_IsAutoFocus},
    {"SetFocus", LuaWidget_SetFocus},
    {"ClearFocus", LuaWidget_ClearFocus},
    {"HasFocus", LuaWidget_HasFocus},
    {"HighlightText", LuaWidget_HighlightText},
    {"SetChecked", LuaWidget_SetChecked},
    {"GetChecked", LuaWidget_GetChecked},
    {"SetFontObject", LuaWidget_SetFontObject},
    {"GetFontObject", LuaWidget_GetFontObject},
    {"GetFrameLevel", LuaWidget_GetFrameLevel},
    {"Raise", LuaWidget_Raise},
    {"Lower", LuaWidget_Lower},
    {"GetScale", LuaWidget_GetScale},
    {"GetEffectiveScale", LuaWidget_GetEffectiveScale},
    {"SetScale", LuaWidget_SetScale},
    {"SetModelScale", LuaWidget_SetModelScale},
    {"SetPosition", LuaWidget_SetPosition},
    {"GetPosition", LuaWidget_GetPosition},
    {"SetFacing", LuaWidget_SetFacing},
    {"GetFacing", LuaWidget_GetFacing},
    {"RegisterForClicks", LuaWidget_RegisterForClicks},
    {"Click", LuaWidget_Click},
    {"LockHighlight", LuaWidget_LockHighlight},
    {"UnlockHighlight", LuaWidget_UnlockHighlight},
    {"GetButtonState", LuaWidget_GetButtonState},
    {"SetButtonState", LuaWidget_SetButtonState},
    {"GetTextWidth", LuaWidget_GetTextWidth},
    {"GetStringWidth", LuaWidget_GetStringWidth},
    {"GetBoundsRect", LuaWidget_GetBoundsRect},
    {"StopAnimating", LuaWidget_StopAnimating},
    {"IsDragging", LuaWidget_IsDragging},
    {"IsMouseOver", LuaWidget_IsMouseOver},
    {"GetAnimationGroups", LuaWidget_GetAnimationGroups},
    {"CreateAnimationGroup", LuaWidget_CreateAnimationGroup},
    {"GetTitleRegion", LuaWidget_GetTitleRegion},
    {"CreateTitleRegion", LuaWidget_CreateTitleRegion},
    {"GetMinMaxValues", LuaWidget_GetMinMaxValues},
    {"SetMinMaxValues", LuaWidget_SetMinMaxValues},
    {"GetValue", LuaWidget_GetValue},
    {"SetValue", LuaWidget_SetValue},
    {"GetValueStep", LuaWidget_GetValueStep},
    {"SetValueStep", LuaWidget_SetValueStep},
    {"SetMaxBytes", LuaWidget_SetMaxBytes},
    {"SetMaxLetters", LuaWidget_SetMaxLetters},
    {"GetInputLanguage", LuaWidget_GetInputLanguage},
    {"GetVerticalScroll", LuaWidget_GetVerticalScroll},
    {"SetVerticalScroll", LuaWidget_SetVerticalScroll},
    {"GetVerticalScrollRange", LuaWidget_GetVerticalScrollRange},
    {"GetHorizontalScroll", LuaWidget_GetHorizontalScroll},
    {"SetHorizontalScroll", LuaWidget_SetHorizontalScroll},
    {"GetHorizontalScrollRange", LuaWidget_GetHorizontalScrollRange},
    {"SetDesaturated", LuaWidget_SetDesaturated},
    {"SetDisabledTextColor", LuaWidget_SetDisabledTextColor},
    {"SetFogColor", LuaWidget_SetFogColor},
    {"SetFogNear", LuaWidget_SetFogNear},
    {"SetFogFar", LuaWidget_SetFogFar},
    {"ClearFog", LuaWidget_ClearFog},
    {"SetGlow", LuaWidget_SetGlow},
    {"ResetLights", LuaWidget_ResetLights},
    {"AddLight", LuaWidget_AddLight},
    {"AddCharacterLight", LuaWidget_AddCharacterLight},
    {"AddPetLight", LuaWidget_AddPetLight},
    {"GetNormalTexture", LuaWidget_GetNormalTexture},
    {"SetNormalTexture", LuaWidget_SetNormalTexture},
    {"GetHighlightTexture", LuaWidget_GetHighlightTexture},
    {"SetHighlightTexture", LuaWidget_SetHighlightTexture},
    {"GetPushedTexture", LuaWidget_GetPushedTexture},
    {"SetPushedTexture", LuaWidget_SetPushedTexture},
    {"GetDisabledTexture", LuaWidget_GetDisabledTexture},
    {"AddLine", LuaWidget_AddLine},
    {"Clear", LuaWidget_Clear},
    {"AdvanceTime", LuaWidget_AdvanceTime},
    {"EnableSubtitles", LuaWidget_EnableSubtitles},
    {"StartMovie", LuaWidget_StartMovie},
    {"StopMovie", LuaWidget_StopMovie},

    {"IsObjectType", LuaWidget_IsObjectType},
    {"GetFont", LuaWidget_GetFont},
    {"GetDrawLayer", LuaWidget_GetDrawLayer},
    {"SetDrawLayer", LuaWidget_SetDrawLayer},
    {"GetShadowColor", LuaWidget_GetShadowColor},
    {"SetShadowColor", LuaWidget_SetShadowColor},
    {"GetShadowOffset", LuaWidget_GetShadowOffset},
    {"SetShadowOffset", LuaWidget_SetShadowOffset},
    {"GetSpacing", LuaWidget_GetSpacing},
    {"SetSpacing", LuaWidget_SetSpacing},
    {"SetTextHeight", LuaWidget_SetTextHeight},
    {"GetStringHeight", LuaWidget_GetStringHeight},
    {"SetAlphaGradient", LuaWidget_SetAlphaGradient},
    {"CanWordWrap", LuaWidget_CanWordWrap},
    {"SetWordWrap", LuaWidget_SetWordWrap},
    {"CanNonSpaceWrap", LuaWidget_CanNonSpaceWrap},
    {"SetNonSpaceWrap", LuaWidget_SetNonSpaceWrap},
    {"SetIndentedWordWrap", LuaWidget_SetIndentedWordWrap},
    {"GetIndentedWordWrap", LuaWidget_GetIndentedWordWrap},
    {"GetJustifyH", LuaWidget_GetJustifyH},
    {"GetJustifyV", LuaWidget_GetJustifyV},

    {"GetFrameStrata", LuaWidget_GetFrameStrata},
    {"SetToplevel", LuaWidget_SetToplevel},
    {"SetMovable", LuaWidget_SetMovable},
    {"IsMovable", LuaWidget_IsMovable},
    {"SetResizable", LuaWidget_SetResizable},
    {"SetClampedToScreen", LuaWidget_SetClampedToScreen},
    {"IsClampedToScreen", LuaWidget_IsClampedToScreen},
    {"EnableMouse", LuaWidget_EnableMouse},
    {"IsMouseEnabled", LuaWidget_IsMouseEnabled},
    {"EnableMouseWheel", LuaWidget_EnableMouseWheel},
    {"IsMouseWheelEnabled", LuaWidget_IsMouseWheelEnabled},
    {"IsKeyboardEnabled", LuaWidget_IsKeyboardEnabled},
    {"EnableJoystick", LuaWidget_EnableJoystick},
    {"IsJoystickEnabled", LuaWidget_IsJoystickEnabled},
    {"RegisterForDrag", LuaWidget_RegisterForDrag},
    {"StartSizing", LuaWidget_StartSizing},
    {"StopMovingOrSizing", LuaWidget_StopMovingOrSizing},
    {"SetUserPlaced", LuaWidget_SetUserPlaced},
    {"IsUserPlaced", LuaWidget_IsUserPlaced},
    {"SetDepth", LuaWidget_SetDepth},
    {"GetDepth", LuaWidget_GetDepth},
    {"GetEffectiveDepth", LuaWidget_GetEffectiveDepth},
    {"IgnoreDepth", LuaWidget_IgnoreDepth},
    {"IsIgnoringDepth", LuaWidget_IsIgnoringDepth},
    {"HasScript", LuaWidget_HasScript},
    {"GetScript", LuaWidget_GetScript},
    {"HookScript", LuaWidget_HookScript},
    {"GetSize", LuaWidget_GetSize},
    {"GetEffectiveAlpha", LuaWidget_GetEffectiveAlpha},
    {"GetBackdrop", LuaWidget_GetBackdrop},
    {"GetBackdropColor", LuaWidget_GetBackdropColor},
    {"GetBackdropBorderColor", LuaWidget_GetBackdropBorderColor},
  };

  for (const auto& m : kMethods) {
    lua_pushcfunction(state, m.fn);
    lua_setfield(state, -2, m.name);
  }

  lua_pushvalue(state, -1);
  lua_setfield(state, -2, "__index");

  lua_setfield(state, LUA_REGISTRYINDEX, kWidgetMethodsRegistryKey);
}

void BindWidgetObjectTypeMethods(lua_State* state,
                                 const std::string& widget_kind) {
  if (!EqualsIgnoreCaseAscii(widget_kind.c_str(), "FontString")) return;
  lua_pushcfunction(state, LuaWidget_FontStringGetObjectType);
  lua_setfield(state, -2, "GetObjectType");
  lua_pushcfunction(state, LuaWidget_IsObjectType);
  lua_setfield(state, -2, "IsObjectType");

  lua_pushcfunction(state, LuaWidget_SetIndentedWordWrap);
  lua_setfield(state, -2, "GetIndentedWordWrap");
  lua_pushcfunction(state, LuaWidget_GetIndentedWordWrap);
  lua_setfield(state, -2, "SetIndentedWordWrap");
}

}

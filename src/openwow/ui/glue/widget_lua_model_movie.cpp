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

int LuaWidget_SetModelScale(lua_State* state) {

  if (lua_istable(state, 1) == 0) return 0;
  const auto name = WidgetNameFromArg(state, 1);
  const float scale = static_cast<float>(luaL_optnumber(state, 2, 1.0));
  if (!name.empty()) {
    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      runtime->SetModelScale(name, scale);
    }
  }
  return 0;
}

int LuaWidget_SetPosition(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const float x = static_cast<float>(luaL_checknumber(state, 2));
  const float y = static_cast<float>(luaL_checknumber(state, 3));
  const float z = static_cast<float>(luaL_checknumber(state, 4));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetModelPosition(name, x, y, z);
  }
  return 0;
}

int LuaWidget_GetPosition(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->GetModelPosition(name, x, y, z);
  }
  lua_pushnumber(state, static_cast<double>(x));
  lua_pushnumber(state, static_cast<double>(y));
  lua_pushnumber(state, static_cast<double>(z));
  return 3;
}

int LuaWidget_SetFacing(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetFacing(facing)", name.c_str());
  }
  const auto facing = static_cast<float>(lua_tonumber(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFacing(name, facing);
  }
  return 0;
}

int LuaWidget_GetFacing(lua_State* state) {
  if (lua_istable(state, 1) == 0) {
    lua_pushnumber(state, 0.0);
    return 1;
  }
  const auto name = WidgetNameFromArg(state, 1);
  float facing = 0.0f;
  if (!name.empty()) {
    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      facing = runtime->GetFacing(name);
    }
  }
  lua_pushnumber(state, static_cast<double>(facing));
  return 1;
}
int LuaWidget_SetFogColor(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const float r = static_cast<float>(luaL_optnumber(state, 2, 0.0));
  const float g = static_cast<float>(luaL_optnumber(state, 3, 0.0));
  const float b = static_cast<float>(luaL_optnumber(state, 4, 0.0));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFogColor(name, r, g, b);
  }
  return 0;
}

int LuaWidget_SetFogNear(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const float near_v = static_cast<float>(luaL_optnumber(state, 2, 0.0));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFogNear(name, near_v);
  }
  return 0;
}

int LuaWidget_SetFogFar(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  const float far_v = static_cast<float>(luaL_optnumber(state, 2, 0.0));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetFogFar(name, far_v);
  }
  return 0;
}

int LuaWidget_ClearFog(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->ClearFog(name);
  }
  return 0;
}

int LuaWidget_SetGlow(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetGlow(value)",
                      name.empty() ? "<unnamed>" : name.c_str());
  }
  const float glow = static_cast<float>(lua_tonumber(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetGlow(name, glow);
  }
  return 0;
}

namespace {

bool GlueWidgetMatchesModelLightType(const std::string& kind) {
  const auto lower_kind = ToLowerAscii(kind);
  return lower_kind == "model" || lower_kind == "modelffx" ||
         lower_kind == "playermodel" || lower_kind == "dressupmodel" ||
         lower_kind == "tabardmodel";
}

std::string GetCheckedGlueModelLightWidgetName(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (IsUiParentName(name)) {
    luaL_error(state, "Wrong object type for member function");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!GlueWidgetMatchesModelLightType(widget->kind)) {
    luaL_error(state, "Wrong object type for member function");
  }
  return name;
}

}

int LuaWidget_ResetLights(lua_State* state) {
  const auto name = GetCheckedGlueModelLightWidgetName(state);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->ResetLights(name);
  }
  return 0;
}

namespace {

constexpr const char* kGlueModelLightUsageAddLight =
    "Usage: %s:AddLight(index, enabled[, omni, dirX, dirY, dirZ, "
    "ambIntensity[, ambR, ambG, ambB], dirIntensity[, dirR, dirG, dirB]])";
constexpr const char* kGlueModelLightUsageAddCharacterLight =
    "Usage: %s:AddCharacterLight(index, enabled[, omni, dirX, dirY, dirZ, "
    "ambIntensity[, ambR, ambG, ambB], dirIntensity[, dirR, dirG, dirB]])";
bool TryReadNumericFlag(lua_State* state, int index, bool* value) {
  if (value == nullptr || lua_isnumber(state, index) == 0) {
    return false;
  }
  *value = openwow::ui::game::detail::TruncateLuaNumberToSseI32(
               lua_tonumber(state, index)) != 0;
  return true;
}

float ClampLightColorComponent(lua_State* state, int index) {
  const double value = lua_tonumber(state, index);
  if (value < 0.0) {
    return 0.0f;
  }
  if (value >= 1.0) {
    return 1.0f;
  }
  return static_cast<float>(value);
}

int ReadGlueModelLightSlot(lua_State* state, int index) {
  bool slot_one = false;
  if (!TryReadNumericFlag(state, index, &slot_one)) {
    return 0;
  }
  return slot_one ? 1 : 0;
}

std::string GetGlueModelLightUsageObjectName(lua_State* state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (!name.empty()) {
    return name;
  }
  return "<unnamed>";
}

bool ParseLightFromStack(lua_State* state, int base,
                         openwow::ui::glue::ModelLightEntry* entry) {
  if (entry == nullptr) {
    return false;
  }

  openwow::ui::glue::ModelLightEntry parsed;
  if (!TryReadNumericFlag(state, base, &parsed.enabled)) {
    return false;
  }
  if (!parsed.enabled) {
    *entry = parsed;
    return true;
  }

  if (!TryReadNumericFlag(state, base + 1, &parsed.omni) ||
      lua_isnumber(state, base + 2) == 0 ||
      lua_isnumber(state, base + 3) == 0 ||
      lua_isnumber(state, base + 4) == 0 ||
      lua_isnumber(state, base + 5) == 0) {
    return false;
  }

  parsed.dir_x = static_cast<float>(lua_tonumber(state, base + 2));
  parsed.dir_y = static_cast<float>(lua_tonumber(state, base + 3));
  parsed.dir_z = static_cast<float>(lua_tonumber(state, base + 4));
  if (!parsed.omni) {
    float direction[3] = {parsed.dir_x, parsed.dir_y, parsed.dir_z};
    openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
        direction);
    parsed.dir_x = direction[0];
    parsed.dir_y = direction[1];
    parsed.dir_z = direction[2];
  }

  const float ambient_intensity =
      static_cast<float>(lua_tonumber(state, base + 5));
  int next_arg = base + 6;
  float ambient_r = 1.0f;
  float ambient_g = 1.0f;
  float ambient_b = 1.0f;
  if (ambient_intensity != 0.0f &&
      lua_isnumber(state, next_arg) != 0 &&
      lua_isnumber(state, next_arg + 1) != 0 &&
      lua_isnumber(state, next_arg + 2) != 0) {
    ambient_r = ClampLightColorComponent(state, next_arg);
    ambient_g = ClampLightColorComponent(state, next_arg + 1);
    ambient_b = ClampLightColorComponent(state, next_arg + 2);
    next_arg += 3;
  }
  parsed.amb_r = ambient_intensity * ambient_r;
  parsed.amb_g = ambient_intensity * ambient_g;
  parsed.amb_b = ambient_intensity * ambient_b;

  if (lua_isnumber(state, next_arg) != 0) {
    const float directional_intensity =
        static_cast<float>(lua_tonumber(state, next_arg));
    float directional_r = 1.0f;
    float directional_g = 1.0f;
    float directional_b = 1.0f;
    if (directional_intensity != 0.0f &&
        lua_isnumber(state, next_arg + 1) != 0 &&
        lua_isnumber(state, next_arg + 2) != 0 &&
        lua_isnumber(state, next_arg + 3) != 0) {
      directional_r = ClampLightColorComponent(state, next_arg + 1);
      directional_g = ClampLightColorComponent(state, next_arg + 2);
      directional_b = ClampLightColorComponent(state, next_arg + 3);
    }
    parsed.dir_r = directional_intensity * directional_r;
    parsed.dir_g = directional_intensity * directional_g;
    parsed.dir_b = directional_intensity * directional_b;
  }

  *entry = parsed;
  return true;
}

int AddGlueModelLight(lua_State* state,
                      openwow::ui::glue::ModelLightCategory category,
                      const char* usage_string) {
  const auto name = GetCheckedGlueModelLightWidgetName(state);
  openwow::ui::glue::ModelLightEntry entry;
  if (!ParseLightFromStack(state, 3, &entry)) {
    return luaL_error(
        state, usage_string, GetGlueModelLightUsageObjectName(state).c_str());
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->AddModelLight(name, category, ReadGlueModelLightSlot(state, 2), entry);
  }
  return 0;
}

}

int LuaWidget_AddLight(lua_State* state) {
  return AddGlueModelLight(state,
                           openwow::ui::glue::ModelLightCategory::kGeneral,
                           kGlueModelLightUsageAddLight);
}

int LuaWidget_AddCharacterLight(lua_State* state) {
  return AddGlueModelLight(state,
                           openwow::ui::glue::ModelLightCategory::kCharacter,
                           kGlueModelLightUsageAddCharacterLight);
}

int LuaWidget_AddPetLight(lua_State* state) {
  return AddGlueModelLight(state,
                           openwow::ui::glue::ModelLightCategory::kPet,
                           kGlueModelLightUsageAddCharacterLight);
}
int LuaWidget_AdvanceTime(lua_State* state) {

  if (lua_istable(state, 1) == 0) {
    return luaL_error(state,
                      "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  const auto name = WidgetNameFromArg(state, 1);
  if (name.empty()) {
    return luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    return luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    return luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }

  const auto kind = ToLowerAscii(widget->kind);
  if (kind != "model" && kind != "modelffx" && kind != "playermodel" && kind != "dressupmodel") {
    return luaL_error(state, "Wrong object type for member function");
  }
  return 0;
}

static std::string GetCheckedGlueMovieFrameWidgetName(lua_State* state) {
  const auto name = GetCheckedGlueWidgetName(state);
  if (IsUiParentName(name)) {
    luaL_error(state, "Wrong object type for member function");
  }

  auto* runtime = GetWidgetRuntime(state);
  if (runtime == nullptr) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  const auto widget = runtime->GetWidget(name);
  if (!widget.has_value()) {
    luaL_error(state, "Attempt to find 'this' in non-framescript object");
  }
  if (!EqualsIgnoreCaseAscii(widget->kind.c_str(), "MovieFrame")) {
    luaL_error(state, "Wrong object type for member function");
  }
  return name;
}

int LuaWidget_EnableSubtitles(lua_State* state) {

  (void)GetCheckedGlueMovieFrameWidgetName(state);
  const bool enable = ScriptReadBoolArgOrDefault(state, 2, true);
  lua_pushboolean(state, enable ? 1 : 0);
  lua_setfield(state, 1, "__ow_subtitles_enabled");
  return 0;
}

int LuaWidget_StartMovie(lua_State* state) {

  const auto widget_name = GetCheckedGlueMovieFrameWidgetName(state);
  if (lua_isstring(state, 2) == 0 || lua_isnumber(state, 3) == 0) {
    return luaL_error(state,
                      "Usage: %s:StartMovie(\"filename\", volume_0_to_255)",
                      widget_name.empty() ? "<unnamed>" : widget_name.c_str());
  }
  size_t filename_length = 0;
  const char* filename = lua_tolstring(state, 2, &filename_length);
  const int volume = static_cast<int>(lua_tonumber(state, 3));

  bool ok = false;
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    ok = runtime->StartMovie(
        std::string(filename != nullptr ? filename : "", filename_length), volume);
    if (ok) {
      runtime->SetMovieWidgetName(widget_name);
    }
  }
  if (ok) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaWidget_StopMovie(lua_State* state) {
  (void)GetCheckedGlueMovieFrameWidgetName(state);
  if (auto* runtime = GetGlueRuntime(state); runtime != nullptr) {
    runtime->StopMovie();
  }
  return 0;
}
int LuaWidget_SetCamera(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto usage_name = GetUsageWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetCamera(index)", usage_name.c_str());
  }

  const int camera = RuntimeModelIndexFromClientU32(ClampLuaNumberToClientU32(state, 2));
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetCamera(name, camera);
  }
  return 0;
}

int LuaWidget_SetSequence(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto usage_name = GetUsageWidgetName(state);
  if (lua_isnumber(state, 2) == 0) {
    return luaL_error(state, "Usage: %s:SetSequence(sequence)", usage_name.c_str());
  }

  const std::uint32_t seq = ClampLuaNumberToClientU32(state, 2);
  if (seq >= kModelSequenceCount) {
    return luaL_error(state,
                      "Error: %s:SetSequence(sequence) exceeds valid range of 0 - %d",
                      usage_name.c_str(),

                      static_cast<int>(kModelSequenceCount));
  }

  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetSequence(name, RuntimeModelIndexFromClientU32(seq));
  }
  return 0;
}

int LuaWidget_SetSequenceTime(lua_State* state) {
  const auto name = GetCheckedGlueFrameWidgetName(state);
  const auto usage_name = GetUsageWidgetName(state);
  if (lua_isnumber(state, 2) == 0 || lua_isnumber(state, 3) == 0) {
    return luaL_error(state,
                      "Usage: %s:SetSequenceTime(sequence, time)",
                      usage_name.c_str());
  }

  const int seq = RuntimeModelIndexFromClientU32(ClampLuaNumberToClientU32(state, 2));
  const std::uint32_t time_ms = ClampLuaNumberToClientU32(state, 3);
  if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
    runtime->SetSequenceTime(name, seq, time_ms);
  }
  return 0;
}

int LuaWidget_SetModel(lua_State* state) {
  if (lua_istable(state, 1) == 0) {
    return 0;
  }
  const auto name = WidgetNameFromArg(state, 1);
  const char* model = luaL_optstring(state, 2, "");
  lua_pushstring(state, model ? model : "");
  lua_setfield(state, 1, "__ow_model");
  if (!name.empty()) {
    if (auto* runtime = GetWidgetRuntime(state); runtime != nullptr) {
      runtime->SetModel(name, model ? std::string(model) : std::string());
    }
  }
  return 0;
}

}

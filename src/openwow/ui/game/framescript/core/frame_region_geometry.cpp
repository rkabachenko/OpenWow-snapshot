#include "openwow/ui/game/framescript/core/frame_region_geometry.h"

#include "openwow/render/resources/fonts/text_layout.h"
#include "openwow/ui/font_string_layout.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"
#include "openwow/ui/game/lua_frame_mutation_policy.h"
#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/core/frame_input_state.h"
#include "openwow/ui/game/framescript/core/frame_runtime_identity.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/ui/script_boolean.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <lua.hpp>

#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>

namespace openwow::ui::game::frame_api {

openwow::ui::DevicePixelsPerUiUnit ScriptFrameUiUnitScale(lua_State* L,
                                                          int frame_index) {
  const auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
  return openwow::ui::ResolveDevicePixelsPerUiUnit(
      manager != nullptr ? manager->screen_height() : 0.0F,
      static_cast<float>(ComputeFrameEffectiveScale(L, frame_index)));
}

bool TryGetScriptFrameDeviceRect(lua_State* L, int frame_index,
                                 ScriptFrameDeviceRect* output) {
  if (output == nullptr) {
    return false;
  }

  auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
  if (manager == nullptr) {
    return false;
  }

  const char* frame_key = GetFrameRuntimeKeyOrName(L, frame_index);
  const auto* rect = frame_key != nullptr && frame_key[0] != '\0'
                         ? manager->retained_layout().FindRect(frame_key)
                         : nullptr;
  std::optional<openwow::ui::framexml::FrameRect> region_fallback;
  if (rect == nullptr) {

    frame_index = lua_absindex(L, frame_index);
    std::string parent_key;
    runtime::GetInternedLuaField(L, frame_index, "__ow_parent");
    if (lua_istable(L, -1) != 0) {
      if (const char* key = GetFrameRuntimeKeyOrName(L, -1);
          key != nullptr && key[0] != '\0') {
        parent_key = key;
      }
    }
    lua_pop(L, 1);
    if (parent_key.empty()) {
      parent_key = openwow::ui::ReadLuaStringField(
                       L, frame_index, "__ow_parent_name")
                       .value_or(std::string{});
    }
    if (!parent_key.empty()) {
      region_fallback = manager->retained_layout().ResolveAnonymousRegionRect(
          L, frame_index, parent_key);
      if (region_fallback.has_value()) {
        rect = &*region_fallback;
      }
    }
    if (rect == nullptr) {
      return false;
    }
  }

  output->left = static_cast<double>(rect->x);
  output->bottom =
      static_cast<double>(manager->screen_height() - (rect->y + rect->height));
  output->width = static_cast<double>(rect->width);
  output->height = static_cast<double>(rect->height);
  return true;
}

bool TryGetScriptFrameRect(lua_State* L, int frame_index,
                           ScriptFrameUiRect* output) {
  if (output == nullptr) {
    return false;
  }
  ScriptFrameDeviceRect device_rect{};
  if (!TryGetScriptFrameDeviceRect(L, frame_index, &device_rect)) {

    return false;
  }

  *output = ScriptFrameRectToUiUnits(device_rect,
                                     ScriptFrameUiUnitScale(L, frame_index));
  return true;
}

bool TryGetScriptFrameBoundsRect(lua_State* L, int frame_index,
                                 ScriptFrameUiRect* output) {
  if (output == nullptr) {
    return false;
  }

  auto* frame = dynamic_cast<openwow::ui::widgets::CSimpleFrame*>(
      lua_adapter::BorrowNativeScriptObject(L, frame_index));
  if (frame == nullptr) {
    return false;
  }

  float bounds[4] = {
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::max(),
      0.0F,
      0.0F,
  };
  if (!frame->GetBoundsRect(bounds)) {
    return false;
  }

  const float effective_scale = frame->GetEffectiveScale();
  if (!std::isfinite(effective_scale) || effective_scale <= 0.0F) {
    return false;
  }

  const float conversion = openwow::ui::GetCachedUiAspectScaleKx() *
                           openwow::ui::kUiScriptCoordinateScale /
                           effective_scale;
  const auto convert = [conversion](const float value) {
    return static_cast<double>(
        openwow::ui::RemoveCachedUiHorizontalStretch(conversion * value));
  };

  output->left = convert(bounds[1]);
  output->bottom = convert(bounds[0]);
  output->width = convert(bounds[3] - bounds[1]);
  output->height = convert(bounds[2] - bounds[0]);
  return true;
}

int SetLuaRegionDimension(lua_State *L, const char *method_name,
                          const char *usage_argument,
                          const char *field_name) {
  const int self_index = ValidateFrameScriptSelf(L);
  if (lua_adapter::IsFrameMutationBlocked(L, self_index)) {
    return 0;
  }
  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:%s(%s)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index),
                      method_name, usage_argument);
  }

  openwow::ui::WriteLuaNumberField(L, self_index, field_name, lua_tonumber(L, 2));
  MarkLuaFontStringDimensionFromLayout(L, self_index, field_name, true);
  NotifyFrameInputMutation(L, self_index, false);
  return 0;
}

int SetLuaRegionSize(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);
  if (lua_adapter::IsFrameMutationBlocked(L, self_index)) {
    return 0;
  }
  if (lua_isnumber(L, 2) == 0 || lua_isnumber(L, 3) == 0) {
    return luaL_error(L, "Usage: %s:SetSize(width, height)",
                      lua_adapter::ScriptObjectDisplayName(L, self_index));
  }

  openwow::ui::WriteLuaNumberField(L, self_index, "__ow_width", lua_tonumber(L, 2));
  openwow::ui::WriteLuaNumberField(L, self_index, "__ow_height", lua_tonumber(L, 3));
  MarkLuaFontStringDimensionFromLayout(L, self_index, "__ow_width", true);
  MarkLuaFontStringDimensionFromLayout(L, self_index, "__ow_height", true);
  NotifyFrameInputMutation(L, self_index, false);
  return 0;
}

static std::optional<float> ReadLuaTableNumberField(lua_State *L, int index,
                                                    const char *field_name) {
  index = lua_absindex(L, index);
  runtime::GetInternedLuaField(L, index, field_name);
  std::optional<float> value;
  if (lua_isnumber(L, -1) != 0) {
    value = static_cast<float>(lua_tonumber(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

static bool LuaFieldEqualsLiteral(lua_State *L, int index,
                                  const char *field_name,
                                  const char *expected) {
  if (lua_istable(L, index) == 0) {
    return false;
  }
  runtime::GetInternedLuaField(L, index, field_name);
  const char *text = lua_tostring(L, -1);
  const bool matches = text != nullptr && std::strcmp(text, expected) == 0;
  lua_pop(L, 1);
  return matches;
}

namespace region_field {

constexpr const char *const kNames[] = {
    "__ow_width",
    "__ow_height",
    "__ow_type",
};

consteval int Ordinal(const std::string_view name) {
  return runtime::InternedLuaFieldKeyOrdinal(kNames, name);
}

using Keys = runtime::InternedLuaFieldKeys;

[[nodiscard]] Keys PushKeys(lua_State *L) {
  return runtime::PushInternedLuaFieldKeys(L, kNames);
}

template <int Key>
void Get(lua_State *L, int index, const Keys keys) {
  runtime::GetInternedLuaBlockField(L, index, keys.index, Key);
}

struct ScopedKeys {
  lua_State *lua;
  int restore_top;
  ~ScopedKeys() { lua_settop(lua, restore_top); }
};

}

#define OW_REGION_KEY(name) ::openwow::ui::game::frame_api::region_field::Ordinal(name)

template <int Key>
static std::optional<float> ReadBlockNumberField(lua_State *L, const int index,
                                                 const region_field::Keys keys) {
  region_field::Get<Key>(L, index, keys);
  std::optional<float> value;
  if (lua_isnumber(L, -1) != 0) {
    value = static_cast<float>(lua_tonumber(L, -1));
  }
  lua_pop(L, 1);
  return value;
}

enum class LuaRegionKind : std::uint8_t { Other, FontString, Texture };

static LuaRegionKind ReadLuaRegionKind(lua_State *L, const int index,
                                       const region_field::Keys keys) {
  if (lua_istable(L, index) == 0) {
    return LuaRegionKind::Other;
  }
  region_field::Get<OW_REGION_KEY("__ow_type")>(L, index, keys);

  const char *const text = lua_tostring(L, -1);
  LuaRegionKind kind = LuaRegionKind::Other;
  if (text != nullptr) {
    if (std::strcmp(text, "FontString") == 0) {
      kind = LuaRegionKind::FontString;
    } else if (std::strcmp(text, "Texture") == 0) {
      kind = LuaRegionKind::Texture;
    }
  }
  lua_pop(L, 1);
  return kind;
}
std::optional<openwow::render::text::TextLayout>
MeasureLuaFontStringMetrics(lua_State *L, int font_string_index);

static constexpr const char kRootFrameName[] = "UIParent";

lua_Number ReadStoredFrameScaleField(lua_State* L, int frame_index) {
  const auto scale = ReadLuaTableNumberField(L, frame_index, "__ow_scale");
  return scale.has_value() ? static_cast<lua_Number>(*scale) : 1.0;
}

static bool IsRootScriptFrame(lua_State* L, int frame_index) {
  if (lua_istable(L, frame_index) == 0) {
    return false;
  }
  const char* const key = GetFrameRuntimeKeyOrName(L, frame_index);
  return key != nullptr && std::strcmp(key, kRootFrameName) == 0;
}

lua_Number ReadFrameScaleFieldOrDefault(lua_State* L, int frame_index) {
  const lua_Number stored = ReadStoredFrameScaleField(L, frame_index);
  if (!IsRootScriptFrame(L, frame_index)) {
    return stored;
  }

  const auto* manager = runtime::WorldUiRuntimeContext::FromLua(L);
  return manager != nullptr ? stored * manager->root_scale() : stored;
}

lua_Number ComputeFrameEffectiveScale(lua_State* L, int frame_index) {
  lua_Number effective = 1.0;
  if (const auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    effective = manager->root_scale();
  }

  lua_pushvalue(L, lua_absindex(L, frame_index));
  constexpr int kMaxDepth = 64;
  for (int depth = 0; depth < kMaxDepth; ++depth) {
    if (lua_istable(L, -1) == 0) {
      break;
    }

    effective *= ReadStoredFrameScaleField(L, -1);

    runtime::GetInternedLuaField(L, -1, "__ow_parent");
    lua_remove(L, -2);
    if (lua_istable(L, -1) == 0) {
      break;
    }
  }
  lua_pop(L, 1);
  return effective;
}

static bool IsLuaFontStringObject(lua_State *L, int index) {
  return LuaFieldEqualsLiteral(L, index, "__ow_type", "FontString");
}

void MarkLuaFontStringDimensionFromLayout(lua_State* L, int index,
                                          const char* field_name,
                                          const bool from_layout) {
  if (!IsLuaFontStringObject(L, index)) {
    return;
  }
  const char* provenance_field = nullptr;
  if (std::strcmp(field_name, "__ow_width") == 0) {
    provenance_field = "__ow_font_width_from_layout";
  } else if (std::strcmp(field_name, "__ow_height") == 0) {
    provenance_field = "__ow_font_height_from_layout";
  }
  if (provenance_field != nullptr) {
    lua_pushboolean(L, from_layout ? 1 : 0);
    lua_setfield(L, lua_absindex(L, index), provenance_field);
  }
}

static LuaRegionSizeValues ResolveLuaTextureEffectiveSize(
    lua_State *L, int self_index, LuaRegionSizeValues size) {
  if (size.width != 0.0f && size.height != 0.0f) {
    return size;
  }

  self_index = lua_absindex(L, self_index);
  const auto* game_ui = runtime::WorldUiRuntimeContext::FromLua(L);
  if (game_ui == nullptr) {
    return size;
  }
  const char* const frame_key = GetFrameRuntimeKeyOrName(L, self_index);
  const openwow::ui::framexml::UiFrame* const authored =
      frame_key != nullptr && frame_key[0] != '\0'
          ? game_ui->frame_store().FindFrame(frame_key)
          : nullptr;
  const runtime::TextureHandleState handle =
      runtime::ResolveTextureHandleState(L, self_index, authored);
  const openwow::ui::TextureNaturalSize natural =
      openwow::ui::ResolveTextureNaturalSize(
          game_ui->retained_layout().texture_natural_size_source(),
          *handle.texture_path, handle.solid_colour);
  if (size.width == 0.0f && natural.width.has_value()) {
    size.width = *natural.width;
  }
  if (size.height == 0.0f && natural.height.has_value()) {
    size.height = *natural.height;
  }
  return size;
}

static LuaRegionSizeValues ResolveLuaFontStringEffectiveSize(
    lua_State *L,
    const int font_string_index,
    const LuaRegionSizeValues explicit_size) {
  LuaRegionSizeValues resolved = explicit_size;

  std::optional<openwow::render::text::TextLayout> measurement;
  if (resolved.width == 0.0f || resolved.height == 0.0f) {
    measurement = MeasureLuaFontStringMetrics(L, font_string_index);
  }

  resolved.width = openwow::ui::ResolveFontStringEffectiveScriptDimension(
      resolved.width, measurement.has_value() ? measurement->width : 0.0f);
  resolved.height = openwow::ui::ResolveFontStringEffectiveScriptDimension(
      resolved.height, measurement.has_value() ? measurement->height : 0.0f);
  return resolved;
}

LuaRegionSizeValues ResolveLuaRegionSizeValues(lua_State *L, int self_index,
                                                      const bool use_explicit) {
  self_index = lua_absindex(L, self_index);
  const int keys_base_top = lua_gettop(L);
  const region_field::Keys keys = region_field::PushKeys(L);
  const region_field::ScopedKeys scoped_keys{L, keys_base_top};

  LuaRegionSizeValues size{
      .width = ReadBlockNumberField<OW_REGION_KEY("__ow_width")>(L, self_index,
                                                                 keys)
                   .value_or(0.0f),
      .height = ReadBlockNumberField<OW_REGION_KEY("__ow_height")>(L, self_index,
                                                                   keys)
                    .value_or(0.0f),
  };
  if (use_explicit) {
    return size;
  }

  const LuaRegionKind kind = ReadLuaRegionKind(L, self_index, keys);
  if (kind == LuaRegionKind::FontString) {
    return ResolveLuaFontStringEffectiveSize(L, self_index, size);
  }

  if (size.width != 0.0f && size.height != 0.0f) {
    return size;
  }

  ScriptFrameUiRect rect{};
  if (!TryGetScriptFrameRect(L, self_index, &rect)) {

    if (kind == LuaRegionKind::Texture) {
      return ResolveLuaTextureEffectiveSize(L, self_index, size);
    }
    return size;
  }

  if (size.width == 0.0f) {
    size.width = static_cast<float>(rect.width);
  }
  if (size.height == 0.0f) {
    size.height = static_cast<float>(rect.height);
  }

  if ((size.width == 0.0f || size.height == 0.0f) &&
      kind == LuaRegionKind::Texture) {
    size = ResolveLuaTextureEffectiveSize(L, self_index, size);
  }
  return size;
}

int LuaRegion_GetWidth(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);
  const LuaRegionSizeValues size =
      ResolveLuaRegionSizeValues(L, self_index,
                                 ScriptReadBoolArgOrDefault(L, 2, false));
  lua_pushnumber(L, size.width);
  return 1;
}

int LuaRegion_GetHeight(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);
  const LuaRegionSizeValues size =
      ResolveLuaRegionSizeValues(L, self_index,
                                 ScriptReadBoolArgOrDefault(L, 2, false));
  lua_pushnumber(L, size.height);
  return 1;
}

int LuaRegion_GetSize(lua_State *L) {
  const int self_index = ValidateFrameScriptSelf(L);
  const LuaRegionSizeValues size =
      ResolveLuaRegionSizeValues(L, self_index,
                                 ScriptReadBoolArgOrDefault(L, 2, false));
  lua_pushnumber(L, size.width);
  lua_pushnumber(L, size.height);
  return 2;
}

void PropagateSharedFontShadowStyle(lua_State *L, int self_index);

}

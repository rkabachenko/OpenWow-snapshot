#pragma once

#include "openwow/ui/ui_coordinate_space.h"

struct lua_State;

namespace openwow::ui::game::frame_api {

struct LuaRegionSizeValues {
  float width{0.0F};
  float height{0.0F};
};

template <openwow::ui::UiCoordinateSpace Space>
struct ScriptFrameRect {
  double left{0.0};
  double bottom{0.0};
  double width{0.0};
  double height{0.0};

  [[nodiscard]] double right() const noexcept { return left + width; }
  [[nodiscard]] double top() const noexcept { return bottom + height; }
  [[nodiscard]] double center_x() const noexcept { return left + width * 0.5; }
  [[nodiscard]] double center_y() const noexcept { return bottom + height * 0.5; }
};

using ScriptFrameUiRect =
    ScriptFrameRect<openwow::ui::UiCoordinateSpace::kUiUnits>;

using ScriptFrameDeviceRect =
    ScriptFrameRect<openwow::ui::UiCoordinateSpace::kDevicePixels>;

[[nodiscard]] constexpr ScriptFrameUiRect ScriptFrameRectToUiUnits(
    const ScriptFrameDeviceRect& rect,
    const openwow::ui::DevicePixelsPerUiUnit scale) noexcept {
  const double divisor = static_cast<double>(scale.value);
  return ScriptFrameUiRect{
      .left = rect.left / divisor,
      .bottom = rect.bottom / divisor,
      .width = rect.width / divisor,
      .height = rect.height / divisor,
  };
}

void MarkLuaFontStringDimensionFromLayout(lua_State* lua, int frame_index,
                                          const char* field_name,
                                          bool from_layout);

int SetLuaRegionDimension(lua_State* lua, const char* method_name,
                          const char* usage_argument,
                          const char* field_name);
int SetLuaRegionSize(lua_State* lua);
int LuaRegion_GetWidth(lua_State* lua);
int LuaRegion_GetHeight(lua_State* lua);
int LuaRegion_GetSize(lua_State* lua);
[[nodiscard]] LuaRegionSizeValues ResolveLuaRegionSizeValues(
    lua_State* lua, int frame_index, bool use_explicit);

[[nodiscard]] double ReadStoredFrameScaleField(lua_State* lua, int frame_index);

[[nodiscard]] double ReadFrameScaleFieldOrDefault(lua_State* lua,
                                                  int frame_index);
[[nodiscard]] double ComputeFrameEffectiveScale(lua_State* lua,
                                                int frame_index);

[[nodiscard]] openwow::ui::DevicePixelsPerUiUnit ScriptFrameUiUnitScale(
    lua_State* lua, int frame_index);

[[nodiscard]] bool TryGetScriptFrameRect(lua_State* lua, int frame_index,
                                         ScriptFrameUiRect* output);

[[nodiscard]] bool TryGetScriptFrameDeviceRect(lua_State* lua, int frame_index,
                                               ScriptFrameDeviceRect* output);

[[nodiscard]] bool TryGetScriptFrameBoundsRect(lua_State* lua, int frame_index,
                                               ScriptFrameUiRect* output);

}

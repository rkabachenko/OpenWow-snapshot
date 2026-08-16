#pragma once

struct lua_State;

namespace openwow::ui::display {
class DisplaySettingsService;
}

namespace openwow::ui::lua {
struct NativeBindingCatalog;
}

namespace openwow::ui::display::lua_adapter {

class FullscreenFrameScalePort {
 public:
  virtual ~FullscreenFrameScalePort() = default;

  [[nodiscard]] virtual float AspectScale() const = 0;
  virtual void Apply(lua_State* lua, int frame_index, float scale) = 0;
};

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
SharedDisplaySettingsNativeBindingCatalog(
    DisplaySettingsService& settings, FullscreenFrameScalePort& frame_scale);

}

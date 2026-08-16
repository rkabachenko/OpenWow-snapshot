#pragma once

#include "openwow/ui/display/settings/display_settings_ports.h"

namespace openwow::render::integration::ui {

class BgfxDisplayDeviceAdapter final
    : public openwow::ui::display::DisplayDevicePort {
 public:
  [[nodiscard]] std::vector<openwow::ui::display::MultisampleFormat>
  AvailableMultisampleFormats(int color_bits,
                              int depth_bits) const override;
  [[nodiscard]] std::optional<openwow::ui::display::VideoCapabilities>
  Capabilities() const override;
  [[nodiscard]] bool PlayerResolutionCapabilityAvailable() const override;
};

}

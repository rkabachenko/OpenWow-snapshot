#pragma once

#include <cstdint>

namespace openwow::ui::framexml {

enum class TextureRole : std::uint8_t {
  Normal,
  ButtonNormal,
  ButtonPushed,
  ButtonDisabled,
  ButtonHighlight,
  CheckButtonChecked,
  CheckButtonDisabledChecked,
  SliderThumb,
  StatusBarFill,
  ColorSelectWheel,
  ColorSelectWheelThumb,
  ColorSelectValue,
  ColorSelectValueThumb,
};

}

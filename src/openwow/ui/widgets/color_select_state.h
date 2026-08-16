#pragma once

#include "openwow/ui/color_math.h"

#include <cstdint>

namespace openwow::ui::widgets {

struct ColorSelectRgb {
  double red{1.0};
  double green{1.0};
  double blue{1.0};
};

struct ColorSelectHsv {
  float hue{0.0F};
  float saturation{0.0F};
  float value{1.0F};
};

class ColorSelectState {
 public:
  [[nodiscard]] const ColorSelectHsv& hsv() const noexcept { return hsv_; }

  [[nodiscard]] ColorSelectRgb rgb() const noexcept {
    const auto rgb = openwow::ui::HSVToRGB(
        hsv_.hue, hsv_.saturation, hsv_.value);
    return {NormalizeByte(QuantizeComponent(rgb.r)),
            NormalizeByte(QuantizeComponent(rgb.g)),
            NormalizeByte(QuantizeComponent(rgb.b))};
  }

  void SetHsv(const float hue, const float saturation,
              const float value) noexcept {
    hsv_ = {.hue = hue, .saturation = saturation, .value = value};
  }

  void SetRgb(const double red, const double green, const double blue) noexcept {
    const ColorSelectRgb quantized{
        NormalizeByte(QuantizeComponent(red)),
        NormalizeByte(QuantizeComponent(green)),
        NormalizeByte(QuantizeComponent(blue)),
    };
    const auto hsv = openwow::ui::RGBToHSV(
        quantized.red, quantized.green, quantized.blue);
    hsv_ = {.hue = static_cast<float>(hsv.h),
            .saturation = static_cast<float>(hsv.s),
            .value = static_cast<float>(hsv.v)};
  }

  [[nodiscard]] std::uint32_t PackArgb(const double alpha = 1.0) const noexcept {
    const ColorSelectRgb color = rgb();
    return (static_cast<std::uint32_t>(QuantizeComponent(alpha)) << 24U) |
           (static_cast<std::uint32_t>(QuantizeComponent(color.red)) << 16U) |
           (static_cast<std::uint32_t>(QuantizeComponent(color.green)) << 8U) |
           static_cast<std::uint32_t>(QuantizeComponent(color.blue));
  }

 private:
  static constexpr double kByteMaximum = 255.0;

  [[nodiscard]] static std::uint8_t QuantizeComponent(
      const double component) noexcept {
    const float value = static_cast<float>(component);
    if (!(value >= 0.0F)) {
      return 0;
    }
    if (value >= 1.0F) {
      return static_cast<std::uint8_t>(kByteMaximum);
    }
    return static_cast<std::uint8_t>(value * kByteMaximum + 0.5F);
  }

  [[nodiscard]] static constexpr double NormalizeByte(
      const std::uint8_t value) noexcept {
    return static_cast<double>(value) / kByteMaximum;
  }

  ColorSelectHsv hsv_{};
};

}

#pragma once

#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openwow::ui::widgets {

enum class SliderOrientation : std::uint8_t { Horizontal, Vertical };

class CSimpleSlider : public CSimpleFrame {
 public:
  CSimpleSlider() : CSimpleFrame(ScriptObjectType::Slider) {}
  ~CSimpleSlider() override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Slider || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "Slider") || CSimpleFrame::IsTypeOf(typeName);
  }

  void SetMinMaxValues(float newMin, float newMax) noexcept {
    const float newRange = newMax - newMin;
    if (!(flags_ & kRangeSet)
        || std::fabs(newRange - range_) >= kEpsilon
        || std::fabs(min_ - newMin) >= kEpsilon) {
      min_   = newMin;
      range_ = newRange;
      flags_ |= (kDirty | kRangeSet);

      if (flags_ & kValueSet)
        SetValue(value_);
    }
  }

  [[nodiscard]] float GetMinValue() const noexcept { return min_; }
  [[nodiscard]] float GetMaxValue() const noexcept { return min_ + range_; }

  void SetValue(float inputValue) noexcept {
    if (!(flags_ & kRangeSet))
      return;

    const float maxVal = min_ + range_;

    float clamped = inputValue;
    if (clamped < min_) clamped = min_;
    if (clamped > maxVal) clamped = maxVal;

    float snapped = SnapToStep(clamped);

    if (maxVal < snapped + step_)
      snapped = maxVal;

    if (snapped - step_ < min_)
      snapped = min_;

    if (!(flags_ & kValueSet)
        || std::fabs(snapped - value_) >= kEpsilon) {
      value_ = snapped;
      flags_ |= (kDirty | kValueSet);

    }
  }

  [[nodiscard]] float GetValue() const noexcept { return value_; }

  void SetValueStep(float newStep) noexcept {
    float s = (newStep >= kEpsilonHalf) ? newStep : kEpsilonHalf;
    if (std::fabs(s - step_) >= kEpsilon) {
      step_ = s;
      if (flags_ & kValueSet)
        SetValue(value_);
    }
  }

  [[nodiscard]] float GetValueStep() const noexcept { return step_; }

  void SetOrientation(SliderOrientation o) noexcept { orientation_ = o; }
  [[nodiscard]] SliderOrientation GetOrientation() const noexcept {
    return orientation_;
  }

  void SetThumbTexture(CSimpleTexture* texture,
                       DrawLayer layer = DrawLayer::Overlay);

  bool SetThumbTextureFromFile(const char* filePath);

  void SetThumbTextureFile(const std::string& tex) {
    SetThumbTextureFromFile(tex.c_str());
  }

  [[nodiscard]] CSimpleTexture* GetThumbTextureObj() const noexcept {
    return thumbTexture_;
  }

  void SetObeyStepOnDrag(bool o) noexcept { obeyStep_ = o; }
  [[nodiscard]] bool GetObeyStepOnDrag() const noexcept { return obeyStep_; }

  void OnResize(const float* newRect) override {
    CSimpleFrame::OnResize(newRect);
    flags_ |= kDirty;
  }

 private:

  [[nodiscard]] float SnapToStep(float v) const noexcept {
    if (step_ == 0.0f)
      return v;
    const float rel   = v - min_;
    const float half  = step_ * 0.5f;
    const float steps = (rel > 0.0f)
        ? static_cast<float>(static_cast<int>((rel + half) / step_))
        : static_cast<float>(static_cast<int>((rel - half) / step_));
    return steps * step_ + min_;
  }

  static constexpr uint32_t kDirty    = 0x1;
  static constexpr uint32_t kRangeSet = 0x2;
  static constexpr uint32_t kValueSet = 0x4;

  static constexpr float kEpsilon     = 0.00000023841858f;
  static constexpr float kEpsilonHalf = 0.00000011920929f;

  uint32_t flags_{0};
  float min_{0.0f};
  float range_{0.0f};
  float value_{0.0f};
  float step_{0.0f};
  SliderOrientation orientation_{SliderOrientation::Vertical};

  CSimpleTexture* thumbTexture_{nullptr};

  bool obeyStep_{false};
};

}

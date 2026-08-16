#pragma once

#include "openwow/input/input_manager.h"
#include "openwow/ui/color_math.h"
#include "openwow/ui/widgets/color_select_state.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <cstdint>

namespace openwow::ui::widgets {

class CSimpleColorSelect : public CSimpleFrame {
 public:

  CSimpleColorSelect() : CSimpleFrame(ScriptObjectType::ColorSelect) {
    EnableMouse(true);

  }
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::ColorSelect || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "ColorSelect") || CSimpleFrame::IsTypeOf(typeName);
  }

  void LoadXML(const void* xmlNode, void* errorHandler) override;

  void SetColorRGB(float r, float g, float b) noexcept {
    colorState_.SetRgb(r, g, b);
    UpdatePackedColorAndRefreshValueBar();
    UpdateWheelThumbPosition();
    UpdateValueThumbPosition();
  }
  void GetColorRGB(float& r, float& g, float& b) const noexcept {
    const auto rgb = colorState_.rgb();
    r = static_cast<float>(rgb.red);
    g = static_cast<float>(rgb.green);
    b = static_cast<float>(rgb.blue);
  }

  void SetColorHSV(float h, float s, float v) noexcept {
    colorState_.SetHsv(h, s, v);
    UpdatePackedColorAndRefreshValueBar();
    UpdateWheelThumbPosition();
    UpdateValueThumbPosition();
  }
  void GetColorHSV(float& h, float& s, float& v) const noexcept {
    const auto& hsv = colorState_.hsv();
    h = hsv.hue;
    s = hsv.saturation;
    v = hsv.value;
  }

  void SetColorWheelTexture(const std::string& tex) { wheelTex_ = tex; }
  void SetColorValueTexture(const std::string& tex) { valueTex_ = tex; }
  void SetColorWheelThumbTexture(const std::string& tex) { wheelThumbTex_ = tex; }
  void SetColorValueThumbTexture(const std::string& tex) { valueThumbTex_ = tex; }

  void SetWheelTextureObj(CSimpleTexture* tex) noexcept { wheelTexture_ = tex; }
  [[nodiscard]] CSimpleTexture* GetWheelTextureObj() const noexcept {
    return wheelTexture_;
  }

  void SetWheelThumbTextureObj(CSimpleTexture* tex) noexcept {
    wheelThumbTexture_ = tex;
  }
  [[nodiscard]] CSimpleTexture* GetWheelThumbTextureObj() const noexcept {
    return wheelThumbTexture_;
  }

  void SetValueTextureObj(CSimpleTexture* tex) noexcept { valueTexture_ = tex; }
  [[nodiscard]] CSimpleTexture* GetValueTextureObj() const noexcept {
    return valueTexture_;
  }

  void SetValueTexture(CSimpleTexture* tex) noexcept;

  void SetValueThumbTextureObj(CSimpleTexture* tex) noexcept {
    valueThumbTexture_ = tex;
  }
  [[nodiscard]] CSimpleTexture* GetValueThumbTextureObj() const noexcept {
    return valueThumbTexture_;
  }

  [[nodiscard]] bool IsWheelDown() const noexcept { return wheelDown_; }
  [[nodiscard]] bool IsValueDown() const noexcept { return valueDown_; }
  void ClearMouseDownFlags() noexcept { wheelDown_ = false; valueDown_ = false; }

  bool OnMouseMove(const void* inputEvent) override {
    if (!wheelDown_ && !valueDown_) {
      return CSimpleFrame::OnMouseMove(inputEvent);
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    if (inputEvent) {
      const auto& event = *static_cast<const openwow::input::InputEvent*>(inputEvent);
      mouseX = static_cast<float>(event.mouseX);
      mouseY = static_cast<float>(event.mouseY);
    }

    if (wheelDown_ && wheelTexture_) {
      ScreenRect wheelRect{};
      if (wheelTexture_->TryGetCachedLayoutRect(&wheelRect)) {
        const float halfW = (wheelRect.right - wheelRect.left) * 0.5f;
        const float halfH = (wheelRect.bottom - wheelRect.top) * 0.5f;
        const float centerX = (wheelRect.left + wheelRect.right) * 0.5f;
        const float centerY = (wheelRect.top + wheelRect.bottom) * 0.5f;

        const float nx = (halfW != 0.0f) ? (mouseX - centerX) / halfW : 0.0f;
        const float ny = (halfH != 0.0f) ? (mouseY - centerY) / halfH : 0.0f;

        constexpr float kPi = 3.1415927f;
        constexpr float kDegreesPerHalfCircle = 180.0f;
        constexpr float kRadiansToDegrees = kDegreesPerHalfCircle / kPi;
        const auto& hsv = colorState_.hsv();
        colorState_.SetHsv(
            (std::atan2(ny, nx) + kPi) * kRadiansToDegrees,
            std::min(std::sqrt(nx * nx + ny * ny), 1.0F), hsv.value);

      }
    }

    if (valueDown_ && valueTexture_) {
      ScreenRect valueRect{};
      if (valueTexture_->TryGetCachedLayoutRect(&valueRect)) {
        const float height = valueRect.bottom - valueRect.top;
        if (height != 0.0f) {
          float t = (mouseY - valueRect.top) / height;
          if (t < 0.0f) t = 0.0f;
          else if (t > 1.0f) t = 1.0f;
          const auto& hsv = colorState_.hsv();
          colorState_.SetHsv(hsv.hue, hsv.saturation, t);
        }
      }
    }

    UpdatePackedColorAndRefreshValueBar();
    UpdateWheelThumbPosition();
    UpdateValueThumbPosition();
    FireOnColorSelect();

    return CSimpleFrame::OnMouseMove(inputEvent);
  }

  bool FireOnMouseDown(uint32_t buttonFlag, float x, float y,
                       const char* buttonName = nullptr) override {
    if (wheelTexture_ && wheelTexture_->GetRect().ContainsPoint(x, y)) {
      wheelDown_ = true;
    }
    if (valueTexture_ && valueTexture_->GetRect().ContainsPoint(x, y)) {
      valueDown_ = true;
    }

    OnMouseMove(nullptr);

    return CSimpleFrame::FireOnMouseDown(buttonFlag, x, y, buttonName);
  }

  bool FireOnMouseUp(uint32_t buttonFlag, float x, float y,
                     const char* buttonName = nullptr) override {
    wheelDown_ = false;

    valueDown_ = false;

    return CSimpleFrame::FireOnMouseUp(buttonFlag, x, y, buttonName);
  }

  void FireOnHide() override {
    wheelDown_ = false;

    valueDown_ = false;

    CSimpleFrame::FireOnHide();
  }

 private:

  void UpdatePackedColorAndRefreshValueBar() noexcept {
    packedColor_ = colorState_.PackArgb();

    if (valueTexture_ != nullptr) {
      const auto& hsv = colorState_.hsv();
      const auto hue_rgb = openwow::ui::HSVToRGB(
          hsv.hue, hsv.saturation, 1.0);
      const TextureGradientColor kBlack{0.0f, 0.0f, 0.0f, 1.0f};
      const TextureGradientColor hue_color{
          static_cast<float>(hue_rgb.r),
          static_cast<float>(hue_rgb.g),
          static_cast<float>(hue_rgb.b),
          1.0f};
      valueTexture_->SetGradient(TextureGradientOrientation::Vertical, kBlack, hue_color);
    }
  }

  void UpdateWheelThumbPosition() noexcept {
    if (wheelTexture_ == nullptr || wheelThumbTexture_ == nullptr) {
      return;
    }

    ScreenRect wheel_rect{};
    if (!wheelTexture_->TryGetCachedLayoutRect(&wheel_rect)) {
      wheel_rect = wheelTexture_->GetRect();
    }

    constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
    const auto& hsv = colorState_.hsv();
    const float radians = hsv.hue * kDegreesToRadians;
    const float radius_x =
        wheel_rect.Width() * 0.5F * std::clamp(hsv.saturation, 0.0F, 1.0F);
    const float radius_y =
        wheel_rect.Height() * 0.5F * std::clamp(hsv.saturation, 0.0F, 1.0F);

    RegionAnchor anchor;
    anchor.point = FramePoint::Center;
    anchor.relativeTo = wheelTexture_;
    anchor.relativePoint = FramePoint::Center;
    anchor.offsetX = std::cos(radians) * radius_x;
    anchor.offsetY = std::sin(radians) * radius_y;
    wheelThumbTexture_->SetPoint(anchor);
  }

  void UpdateValueThumbPosition() noexcept {
    if (valueTexture_ == nullptr || valueThumbTexture_ == nullptr) {
      return;
    }

    ScreenRect value_rect{};
    if (!valueTexture_->TryGetCachedLayoutRect(&value_rect)) {
      value_rect = valueTexture_->GetRect();
    }

    RegionAnchor anchor;
    anchor.point = FramePoint::Center;
    anchor.relativeTo = valueTexture_;
    anchor.relativePoint = FramePoint::Top;
    anchor.offsetY = value_rect.Height() *
                     std::clamp(colorState_.hsv().value, 0.0F, 1.0F);
    valueThumbTexture_->SetPoint(anchor);
  }

  void FireOnColorSelect() noexcept {

    if (HasScript("OnColorSelect")) {

      RunScript("OnColorSelect");
    }
  }

  ColorSelectState colorState_{};

  std::string wheelTex_, valueTex_, wheelThumbTex_, valueThumbTex_;

  CSimpleTexture* wheelTexture_{nullptr};

  CSimpleTexture* wheelThumbTexture_{nullptr};

  CSimpleTexture* valueTexture_{nullptr};

  CSimpleTexture* valueThumbTexture_{nullptr};

  bool wheelDown_{false};

  bool valueDown_{false};

  uint32_t packedColor_{0};

};

}

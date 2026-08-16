
#pragma once

#include "openwow/ui/widgets/script_region.h"

#include "openwow/foundation/math/float_compare.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace openwow::ui::widgets {

class CSimpleFontString;

struct FontShadow {
  float offsetX{0.0f};
  float offsetY{0.0f};
  float r{0.0f}, g{0.0f}, b{0.0f}, a{1.0f};
};

class CSimpleFont : public CScriptRegion {
public:
  explicit CSimpleFont(ScriptObjectType type) noexcept : CScriptRegion(type) {}

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Font || CScriptRegion::IsKindOf(t);
  }

  [[nodiscard]] bool IsTypeOf(const char *typeName) const noexcept override {
    return StrCaseEq(typeName, "Font") || CScriptRegion::IsTypeOf(typeName);
  }

  void SetFontFile(const std::string &path) {
    fontFile_ = path;
  }
  [[nodiscard]] const std::string &GetFontFile() const noexcept {
    return fontFile_;
  }

  void SetFontSize(float size) noexcept {
    fontSize_ = size;
  }
  [[nodiscard]] float GetFontSize() const noexcept {
    return fontSize_;
  }

  void SetFontFlags(const std::string &flags);
  [[nodiscard]] const std::string &GetFontFlags() const noexcept {
    return fontFlags_;
  }
  void SetFontFlagsBits(std::uint32_t flags);
  [[nodiscard]] std::uint32_t GetFontFlagsBits() const noexcept {
    return fontFlagsBits_;
  }

  bool SetFont(const std::string &path, float size, std::uint32_t flags);
  bool SetFont(const std::string &path, float size, const std::string &flags = "");

  [[nodiscard]] static bool CanUseStoredFontFace(const std::string &path, float size);

  void SetTextColor(float r, float g, float b, float a = 1.0f) noexcept {
    const auto red = QuantizePackedColorComponent(r);
    const auto green = QuantizePackedColorComponent(g);
    const auto blue = QuantizePackedColorComponent(b);
    const auto alpha = QuantizePackedColorComponent(a);
    const auto requested_color = PackStoredColorBytesBGRA(red, green, blue, alpha);

    if ((styleFlags_ & 0x400u) == 0 || requested_color != GetPackedTextColor()) {
      textR_ = NormalizePackedColorComponent(red);
      textG_ = NormalizePackedColorComponent(green);
      textB_ = NormalizePackedColorComponent(blue);
      textA_ = NormalizePackedColorComponent(alpha);
      styleFlags_ |= 0x404u;
    }

    dirtyFlags_ &= ~0x4u;
    OnTextColorChanged(textR_, textG_, textB_, textA_);
  }
  void GetTextColor(float &r, float &g, float &b, float &a) const noexcept {
    ResolveTextColor(r, g, b, a);
  }

  void SetShadowColor(float r, float g, float b, float a = 1.0f) noexcept {
    (void)UpdateStoredShadowColor(r, g, b, a);
    dirtyFlags_ &= ~0x8u;
  }

  void SetShadowOffset(float x, float y) noexcept {
    if (shadow_.offsetX != x || shadow_.offsetY != y ||
        (styleFlags_ & 0x800u) == 0) {
      shadow_.offsetX = x;
      shadow_.offsetY = y;
      styleFlags_ |= 0x808u;
    }
  }
  [[nodiscard]] const FontShadow &GetShadow() const noexcept {
    return shadow_;
  }

  void SetSpacing(float spacing) noexcept {
    if ((styleFlags_ & 0x1000u) == 0 ||
        openwow::math::float_compare::OutsideTolerance(
            spacing, spacing_,
            openwow::math::float_compare::kClientFloatEpsilon)) {
      spacing_ = spacing;
      styleFlags_ |= 0x1010u;
    }
    dirtyFlags_ &= ~0x10u;
    OnSpacingChanged();
  }
  [[nodiscard]] float GetSpacing() const noexcept {
    return spacing_;
  }

  void SetFontObject(CSimpleFont *fontObj) noexcept {
    fontObject_ = fontObj;
  }
  [[nodiscard]] CSimpleFont *GetFontObject() const noexcept {
    return fontObject_;
  }

  static CSimpleFont *Create(ScriptObjectType type = ScriptObjectType::Font) {
    return new CSimpleFont(type);
  }

  void LoadXML(const void *xmlNode, float , int errorHandler);

  void CopyStyleFrom(const CSimpleFont &source);

  void CopyMaskedStyleFrom(const CSimpleFont &source, uint32_t observerMask);

  void ApplyStyleFromMask(CSimpleFontString *target, int applyMask) const;

  void SetStyleFlags(uint32_t flags) noexcept {
    styleFlags_ = flags;
  }
  [[nodiscard]] uint32_t GetStyleFlags() const noexcept {
    return styleFlags_;
  }

  void SetDirtyFlags(uint32_t flags) noexcept {
    dirtyFlags_ = flags;
  }
  [[nodiscard]] uint32_t GetDirtyFlags() const noexcept {
    return dirtyFlags_;
  }

  void SetJustifyH(uint8_t justifyH) noexcept {
    const uint8_t masked = justifyH & 0x07u;
    if (masked != justifyH_ || (styleFlags_ & 0x200u) == 0) {
      styleFlags_ |= 0x202u;
      justifyH_ = masked;
    }
  }
  [[nodiscard]] uint8_t GetJustifyH() const noexcept {
    return justifyH_;
  }

  void SetJustifyV(uint8_t justifyV) noexcept {
    const uint8_t masked = justifyV & 0x38u;
    if (masked != justifyV_ || (styleFlags_ & 0x200u) == 0) {
      styleFlags_ |= 0x202u;
      justifyV_ = masked;
    }
  }
  [[nodiscard]] uint8_t GetJustifyV() const noexcept {
    return justifyV_;
  }

  void SetWordWrapFlags(uint32_t flags) noexcept {
    wordWrapFlags_ = flags;
  }
  [[nodiscard]] uint32_t GetWordWrapFlags() const noexcept {
    return wordWrapFlags_;
  }

protected:
  virtual void OnTextColorChanged(float , float , float , float ) noexcept {}

  virtual void OnSpacingChanged() noexcept {}

  virtual void ResolveTextColor(float &r, float &g, float &b, float &a) const noexcept {
    r = textR_;
    g = textG_;
    b = textB_;
    a = textA_;
  }

  [[nodiscard]] bool UpdateStoredShadowColor(float r, float g, float b,
                                             float a) noexcept {
    const auto red = QuantizePackedColorComponent(r);
    const auto green = QuantizePackedColorComponent(g);
    const auto blue = QuantizePackedColorComponent(b);
    const auto alpha = QuantizePackedColorComponent(a);
    const auto requested_color = PackStoredColorBytesBGRA(red, green, blue, alpha);

    if ((styleFlags_ & 0x800u) != 0 &&
        requested_color == GetPackedShadowColor()) {
      return false;
    }

    shadow_.r = NormalizePackedColorComponent(red);
    shadow_.g = NormalizePackedColorComponent(green);
    shadow_.b = NormalizePackedColorComponent(blue);
    shadow_.a = NormalizePackedColorComponent(alpha);
    styleFlags_ |= 0x808u;
    dirtyFlags_ &= ~0x8u;
    return true;
  }

  [[nodiscard]] uint32_t GetPackedTextColor() const noexcept {
    return PackStoredColorBytesBGRA(QuantizePackedColorComponent(textR_),
                                    QuantizePackedColorComponent(textG_),
                                    QuantizePackedColorComponent(textB_),
                                    QuantizePackedColorComponent(textA_));
  }

  [[nodiscard]] uint32_t GetPackedShadowColor() const noexcept {
    return PackStoredColorBytesBGRA(QuantizePackedColorComponent(shadow_.r),
                                    QuantizePackedColorComponent(shadow_.g),
                                    QuantizePackedColorComponent(shadow_.b),
                                    QuantizePackedColorComponent(shadow_.a));
  }

  [[nodiscard]] static uint8_t QuantizePackedColorComponent(float component) noexcept {
    if (std::isnan(component) || !(component < 1.0f)) {
      return 0xFF;
    }
    if (component < 0.0f) {
      return 0;
    }

    int quantized = static_cast<int>(component * 255.0f + 0.5f);
    if (quantized < 0) {
      quantized = 0;
    } else if (quantized > 255) {
      quantized = 255;
    }
    return static_cast<uint8_t>(quantized);
  }

  [[nodiscard]] static float NormalizePackedColorComponent(uint8_t component) noexcept {
    return static_cast<float>(component) / 255.0f;
  }

  [[nodiscard]] static uint32_t PackStoredColorBytesBGRA(uint8_t red, uint8_t green,
                                                         uint8_t blue,
                                                         uint8_t alpha) noexcept {
    return static_cast<uint32_t>(blue) |
           (static_cast<uint32_t>(green) << 8) |
           (static_cast<uint32_t>(red) << 16) |
           (static_cast<uint32_t>(alpha) << 24);
  }

  [[nodiscard]] static uint32_t PackRenderableColorBytesRGBA(uint8_t red, uint8_t green,
                                                             uint8_t blue,
                                                             uint8_t alpha) noexcept {
    return static_cast<uint32_t>(red) |
           (static_cast<uint32_t>(green) << 8) |
           (static_cast<uint32_t>(blue) << 16) |
           (static_cast<uint32_t>(alpha) << 24);
  }

  std::string fontFile_;
  float fontSize_{0.0f};

  std::string fontFlags_;
  std::uint32_t fontFlagsBits_{0};
  float textR_{1.0f}, textG_{1.0f}, textB_{1.0f}, textA_{1.0f};
  FontShadow shadow_;
  float spacing_{0.0f};
  CSimpleFont *fontObject_{nullptr};
  uint32_t styleFlags_{0};

  uint32_t dirtyFlags_{0x1F};

  uint8_t justifyH_{2};

  uint8_t justifyV_{0x10};

  uint32_t wordWrapFlags_{0};

};

}

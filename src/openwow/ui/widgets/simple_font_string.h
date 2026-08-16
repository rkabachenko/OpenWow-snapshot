#pragma once

#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/ui/widgets/simple_font.h"
#include "openwow/ui/widgets/simple_render.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace openwow::ui {
class FontDefinitionRegistry;
namespace xml {
struct ErrorContext;
struct XMLFrameDef;
}
}

namespace openwow::ui::widgets {

enum class JustifyH : uint8_t { Left, Center, Right };
enum class JustifyV : uint8_t { Top, Middle, Bottom };

class CSimpleFontString : public CSimpleFont {
public:
  CSimpleFontString() : CSimpleFont(ScriptObjectType::FontString) {
    BindRenderOwner();
  }

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::FontString || CSimpleFont::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char *typeName) const noexcept override {
    return StrCaseEq(typeName, "FontString") || CSimpleFont::IsTypeOf(typeName);
  }

  void SetWidth(float w) noexcept override {
    CScriptRegion::SetWidth(w);
    MutableRender().ClearCachedDimensions();
  }
  void SetHeight(float h) noexcept override {
    CScriptRegion::SetHeight(h);
    MutableRender().ClearCachedDimensions();
  }
  void SetSize(float w, float h) noexcept override {
    CScriptRegion::SetSize(w, h);
    MutableRender().ClearCachedDimensions();
  }

  void SetText(const std::string &text) {
    MutableRender().SetText(text.c_str(), true);
  }
  [[nodiscard]] std::string GetText() const {
    return std::string(render_.GetText());
  }

  void SetFormattedText(const std::string &text) {
    MutableRender().SetText(text.c_str(), true);
  }

  void AllocTextBuffer(uint16_t size) {
    MutableRender().AllocTextBuffer(size);
  }

  void SetJustifyH(JustifyH j) noexcept {
    UpdateTextLayoutFlagsMasked(HorizontalJustifyToFlags(j), kHorizontalJustifyMask);
  }
  [[nodiscard]] JustifyH GetJustifyH() const noexcept {
    return HorizontalFlagsToJustify(render_.GetHorizontalJustify());
  }

  void SetJustifyV(JustifyV j) noexcept {
    UpdateTextLayoutFlagsMasked(VerticalJustifyToFlags(j), kVerticalJustifyMask);
  }
  [[nodiscard]] JustifyV GetJustifyV() const noexcept {
    return VerticalFlagsToJustify(render_.GetVerticalJustify());
  }

  void SetShadowColor(float r, float g, float b, float a = 1.0f) noexcept {
    CSimpleFont::SetShadowColor(r, g, b, a);

    auto &render = MutableRender();
    if (render.HasActiveTextShadow()) {
      const auto &shadow = GetShadow();
      render.SetActiveTextShadow(GetPackedShadowColor(), shadow.offsetX,
                                 shadow.offsetY);
    }
  }

  void SetShadowOffset(float x, float y) noexcept {
    const auto &shadow = GetShadow();
    if (shadow.offsetX == x && shadow.offsetY == y) {
      return;
    }

    CSimpleFont::SetShadowOffset(x, y);
    auto &render = MutableRender();
    if (x == 0.0f && y == 0.0f) {
      render.ClearActiveTextShadow();
    } else {
      render.SetActiveTextShadow(GetPackedShadowColor(), x, y);
    }

    render.Invalidate();
  }

  void ClearActiveTextShadow() noexcept {
    auto &render = MutableRender();
    render.ClearActiveTextShadow();
  }

  void ApplyShadowStyle(const FontShadow &shadow) noexcept {
    auto &render = MutableRender();
    CSimpleFont::SetShadowColor(shadow.r, shadow.g, shadow.b, shadow.a);
    CSimpleFont::SetShadowOffset(shadow.offsetX, shadow.offsetY);
    render.SetActiveTextShadow(GetPackedShadowColor(), shadow.offsetX,
                               shadow.offsetY);
  }

  void SetWordWrap(bool wrap) noexcept {
    UpdateTextLayoutFlagsMasked(wrap ? 0u : kWordWrapDisabledFlag, kWordWrapDisabledFlag);
  }
  [[nodiscard]] bool GetWordWrap() const noexcept {
    return (render_.GetJustifyFlags() & kWordWrapDisabledFlag) == 0u;
  }

  void SetNonSpaceWrap(bool wrap) noexcept {
    UpdateTextLayoutFlagsMasked(wrap ? kNonSpaceWrapFlag : 0u, kNonSpaceWrapFlag);
  }
  [[nodiscard]] bool GetNonSpaceWrap() const noexcept {
    return (render_.GetJustifyFlags() & kNonSpaceWrapFlag) != 0u;
  }

  void SetMaxLines(uint32_t lines) noexcept {
    maxLines_ = lines;
  }
  [[nodiscard]] uint32_t GetMaxLines() const noexcept {
    return maxLines_;
  }

  void SetLeftMargin(int32_t margin) noexcept { leftMargin_ = margin; }
  [[nodiscard]] int32_t GetLeftMargin() const noexcept { return leftMargin_; }

  void SetRightMargin(int32_t margin) noexcept { rightMargin_ = margin; }
  [[nodiscard]] int32_t GetRightMargin() const noexcept { return rightMargin_; }

  void SetIndentedWordWrap(bool iw) noexcept {
    UpdateTextLayoutFlagsMasked(iw ? kIndentedWordWrapFlag : 0u, kIndentedWordWrapFlag);
  }
  [[nodiscard]] bool GetIndentedWordWrap() const noexcept {
    return (render_.GetJustifyFlags() & kIndentedWordWrapFlag) != 0u;
  }

  [[nodiscard]] float GetStringWidth() const noexcept;
  [[nodiscard]] float GetStringHeight() const noexcept;
  void SetStringMetrics(float w, float h) noexcept {
    MutableRender().SetCachedDimensions(w, h);
  }

  [[nodiscard]] int CountLeadingCharsWithinWidth(
      const char* text, int length, float width) const;

  [[nodiscard]] int CountTrailingCharsWithinWidth(
      const char* text, int length, float width) const;

  [[nodiscard]] float MeasureSubstringWidth(const char* text, int length) const;

  void SetTextHeight(float height) noexcept;
  bool SetFont(const char *path, float height, std::uint32_t fontFlags, bool forced = false);
  bool SetFont(const std::string &path, float height, std::uint32_t fontFlags,
               bool forced = false) {
    return SetFont(path.c_str(), height, fontFlags, forced);
  }
  bool SetFont(const std::string &path, float height, const std::string &flags) {
    return SetFont(path.c_str(), height, openwow::render::ParseFontFlagsString(flags), false);
  }

  bool SetAlphaGradient(int start, int length);
  [[nodiscard]] uint16_t GetAlphaGradientStart() const noexcept {
    return alphaGradientStart_;
  }
  [[nodiscard]] uint16_t GetAlphaGradientLength() const noexcept {
    return alphaGradientLength_;
  }

  [[nodiscard]] bool IsTruncated() const noexcept {
    return truncated_;
  }
  void SetTruncated(bool t) noexcept {
    truncated_ = t;
  }

  void InvalidateText() {
    MutableRender().Invalidate();
  }
  void RefreshTextLayout() {
    UpdateTextLayout();
  }
  void OnLayout() override {
    ClearLayoutDirty();
    MutableRender().SetLayoutValid();
    UpdateTextLayout();
  }

  void OnRectChanged(const ScreenRect &oldRect) override;

  void OnParentAlphaChanged(bool forceRefresh) override;
  void OnFrameScaleChanged(float effectiveScale, bool force) override;
  void OnFrameDepthChanged(float effectiveDepth, bool force) override;
  void OnParentFontHeightChanged(float effectiveHeight, bool force) override;

  [[nodiscard]] CSimpleRender &GetRender() noexcept {
    return MutableRender();
  }
  [[nodiscard]] const CSimpleRender &GetRender() const noexcept {
    return render_;
  }

  void RegisterRenderCallbacks(SimpleRenderBatchSink &sink) const override {
    render_.AddToRenderBatch(sink);
  }

  void LoadXML(const openwow::ui::xml::XMLFrameDef &frame_def,
               openwow::ui::xml::ErrorContext *error_handler = nullptr,
               const openwow::ui::FontDefinitionRegistry *font_registry = nullptr);

  void ApplyDefaultParentAnchor() noexcept;

  void ApplyStyleTextLayoutFlags(std::uint32_t flags) noexcept {
    UpdateTextLayoutFlagsMasked(flags & kStyleTextLayoutMask, kStyleTextLayoutMask);
  }

  [[nodiscard]] std::vector<std::uint32_t> CollectLineStartOffsets(
      std::string_view text, float max_width) const;

protected:
  void OnTextColorChanged(float r, float g, float b, float a) noexcept override {
    auto &render = MutableRender();
    render.SetTextColor(PackRenderableColorBytesRGBA(QuantizePackedColorComponent(r),
                                                     QuantizePackedColorComponent(g),
                                                     QuantizePackedColorComponent(b),
                                                     QuantizePackedColorComponent(a)));
    render.SyncEmbeddedTextureAlphaFromTextColor();
  }

  void OnSpacingChanged() noexcept override;

  void ResolveTextColor(float &r, float &g, float &b, float &a) const noexcept override {
    render_.GetColorFloats(r, g, b, a);
  }

private:
  bool LoadXMLWithInheritance(
      const openwow::ui::xml::XMLFrameDef &frame_def,
      openwow::ui::xml::ErrorContext *error_handler,
      const openwow::ui::FontDefinitionRegistry *font_registry,
      std::unordered_set<std::string> *inheritance_stack);

  static constexpr std::uint32_t kHorizontalJustifyMask = 0x7u;
  static constexpr std::uint32_t kVerticalJustifyMask = 0x38u;
  static constexpr std::uint32_t kWordWrapDisabledFlag = 0x40u;
  static constexpr std::uint32_t kNonSpaceWrapFlag = 0x1000u;
  static constexpr std::uint32_t kIndentedWordWrapFlag = 0x20000u;
  static constexpr std::uint32_t kStyleTextLayoutMask =
      kHorizontalJustifyMask | kVerticalJustifyMask | kWordWrapDisabledFlag |
      kNonSpaceWrapFlag | kIndentedWordWrapFlag;

  static constexpr std::uint32_t HorizontalJustifyToFlags(JustifyH justify) noexcept {
    switch (justify) {
      case JustifyH::Left:
        return 1u;
      case JustifyH::Right:
        return 4u;
      case JustifyH::Center:
      default:
        return 2u;
    }
  }

  static constexpr std::uint32_t VerticalJustifyToFlags(JustifyV justify) noexcept {
    switch (justify) {
      case JustifyV::Top:
        return 8u;
      case JustifyV::Bottom:
        return 32u;
      case JustifyV::Middle:
      default:
        return 16u;
    }
  }

  static constexpr JustifyH HorizontalFlagsToJustify(const std::uint32_t flags) noexcept {
    switch (flags & kHorizontalJustifyMask) {
      case 1u:
        return JustifyH::Left;
      case 4u:
        return JustifyH::Right;
      case 2u:
      default:
        return JustifyH::Center;
    }
  }

  static constexpr JustifyV VerticalFlagsToJustify(const std::uint32_t flags) noexcept {
    switch (flags & kVerticalJustifyMask) {
      case 8u:
        return JustifyV::Top;
      case 32u:
        return JustifyV::Bottom;
      case 16u:
      default:
        return JustifyV::Middle;
    }
  }

  void UpdateTextLayoutFlagsMasked(const std::uint32_t flags, const std::uint32_t mask) noexcept {
    auto &render = MutableRender();
    const auto current_flags = render.GetJustifyFlags();
    const auto next_flags = (current_flags & ~mask) | (flags & mask);
    if (next_flags == current_flags) {
      return;
    }

    render.SetJustifyFlags(next_flags);
    if (render.GetTextLayout().has_value()) {
      render.Invalidate();
    }
  }

  void BindRenderOwner() noexcept {
    render_.SetLayoutOwner(this);
  }
  void UpdateMeasuredStringMetrics() const noexcept;
  [[nodiscard]] CSimpleRender &MutableRender() noexcept {
    BindRenderOwner();
    return render_;
  }

  void UpdateTextLayout();

  CSimpleRender render_;

  uint16_t alphaGradientStart_{0};
  uint16_t alphaGradientLength_{0};
  uint32_t maxLines_{0};
  int32_t leftMargin_{0};

  int32_t rightMargin_{0};

  bool truncated_{false};
};

}

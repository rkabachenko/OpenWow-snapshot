#include "openwow/ui/widgets/simple_font_string.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/game/localization.h"
#include "openwow/foundation/math/float_compare.h"
#include "openwow/render/resources/fonts/font_string_flags.h"
#include "openwow/render/resources/fonts/text_alpha_gradient.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/framexml_font_registry.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_pixel_snap.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/widget_xml_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace openwow::ui::widgets {

namespace {
constexpr float kClientScaleEpsilon = 0.00000023841858f;
constexpr float kMinimumInheritedTextDepth = 0.2f;

float EffectiveScale(const CSimpleRender& render) {
  return render.GetScaleFactor() > 0.0f ? render.GetScaleFactor() : 1.0f;
}

int FontPixelHeight(const CSimpleRender& render, const float stored_height) {

  return std::max(
      2, openwow::render::ClampRetailFontPixelHeight(
             static_cast<int>(std::lround(
                 openwow::ui::StoredUiHorizontalCoordinateToPixels(
                     stored_height) *
                 EffectiveScale(render)))));
}

openwow::render::text::FontStyle ResolveFontStyle(
    const std::uint32_t flags) {
  openwow::render::text::FontOutline outline =
      openwow::render::text::FontOutline::None;
  if (openwow::render::FontFlagsIsThickOutline(flags)) {
    outline = openwow::render::text::FontOutline::Thick;
  } else if (openwow::render::FontFlagsHasOutline(flags)) {
    outline = openwow::render::text::FontOutline::Normal;
  }
  return {.outline = outline,
          .monochrome = openwow::render::FontFlagsIsMonochrome(flags)};
}

openwow::render::text::TextLayoutRequest BuildLayoutRequest(
    const CSimpleFontString& font_string, const float maximum_width,
    const float maximum_height) {
  const auto &render = font_string.GetRender();
  const float scale = EffectiveScale(render);
  openwow::render::text::TextLayoutRequest request;
  request.maximum_width = std::max(0.0f, maximum_width) * scale;
  request.maximum_height = std::max(0.0f, maximum_height) * scale;
  request.line_spacing =
      openwow::ui::StoredUiHorizontalCoordinateToPixels(
          openwow::ui::LegacyPixelSnapUiVerticalCoordinate(
              font_string.GetSpacing())) *
      scale;
  request.maximum_lines = font_string.GetMaxLines();
  request.wrap = openwow::render::text::ResolveWrapMode(
      (render.GetJustifyFlags() & 0x40u) == 0u,
      (render.GetJustifyFlags() & 0x1000u) != 0u);
  request.indent_continuation_lines =
      (render.GetJustifyFlags() & 0x20000u) != 0u;
  request.inline_image_scale = 1.0f;
  return request;
}

std::optional<openwow::render::text::TextLayout>
MeasureLiveFontStringMetrics(const CSimpleFontString &font_string) {
  const auto &render = font_string.GetRender();
  const auto& face = render.GetFontFace();
  const std::string_view text = render.GetText();
  if (!face || text.empty()) return std::nullopt;

  return openwow::render::text::LayoutText(
      *face, text,
      BuildLayoutRequest(font_string, font_string.CScriptRegion::GetWidth(),
                         font_string.CScriptRegion::GetHeight()));
}

std::uint32_t BuildXmlFontFlags(const char *outline, const bool monochrome) {
  std::uint32_t font_flags = 0;
  if (outline != nullptr && *outline != '\0') {
    if (openwow::text::EqualsIgnoreCaseAscii(outline, "NORMAL")) {
      font_flags = openwow::render::kFontFlagOutlineNormal;
    } else if (openwow::text::EqualsIgnoreCaseAscii(outline, "THICK")) {
      font_flags = openwow::render::kFontFlagOutlineThick;
    }
  }

  if (monochrome) {
    font_flags |= openwow::render::kFontFlagMonochrome;
  }
  return font_flags;
}

bool TryParseJustifyBits(const char *value, const bool horizontal, std::uint8_t *out_bits) {
  if (value == nullptr || *value == '\0' || out_bits == nullptr) {
    return false;
  }

  if (horizontal) {
    if (openwow::text::EqualsIgnoreCaseAscii(value, "LEFT")) {
      *out_bits = 1u;
      return true;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(value, "CENTER")) {
      *out_bits = 2u;
      return true;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(value, "RIGHT")) {
      *out_bits = 4u;
      return true;
    }
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(value, "TOP")) {
    *out_bits = 8u;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "MIDDLE")) {
    *out_bits = 16u;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "BOTTOM")) {
    *out_bits = 32u;
    return true;
  }
  return false;
}

const char *FontStringDisplayName(const CSimpleFontString &font_string) {
  return font_string.GetDisplayName();
}

void ApplyResolvedFontDefinition(CSimpleFontString *target,
                                 const openwow::ui::FontDefinition &definition) {
  if (target == nullptr) {
    return;
  }

  CSimpleFont source(ScriptObjectType::Font);
  if (!definition.font_file.empty() && definition.has_height) {
    (void)source.SetFont(definition.font_file, definition.height,
                         BuildXmlFontFlags(definition.outline.c_str(),
                                           definition.monochrome));
  }

  bool has_layout_group = false;
  if (definition.has_justify_h) {
    std::uint8_t bits = 0;
    if (TryParseJustifyBits(definition.justify_h.c_str(), true, &bits)) {
      source.SetJustifyH(bits);
      has_layout_group = true;
    }
  }
  if (definition.has_justify_v) {
    std::uint8_t bits = 0;
    if (TryParseJustifyBits(definition.justify_v.c_str(), false, &bits)) {
      source.SetJustifyV(bits);
      has_layout_group = true;
    }
  }
  if (definition.has_non_space_wrap) {
    std::uint32_t flags = source.GetWordWrapFlags();
    flags = definition.non_space_wrap ? (flags | 0x1000u) : (flags & ~0x1000u);
    source.SetWordWrapFlags(flags);
    has_layout_group = true;
  }
  if (definition.has_indented_word_wrap) {
    std::uint32_t flags = source.GetWordWrapFlags();
    flags = definition.indented_word_wrap ? (flags | 0x20000u) : (flags & ~0x20000u);
    source.SetWordWrapFlags(flags);
    has_layout_group = true;
  }
  if (has_layout_group) {
    source.SetStyleFlags(source.GetStyleFlags() | 0x200u);
  }

  if (definition.has_color) {
    source.SetTextColor(definition.color.r, definition.color.g, definition.color.b,
                        definition.color.a);
  }

  if (definition.shadow.has_value()) {
    source.SetShadowColor(definition.shadow->color.r, definition.shadow->color.g,
                          definition.shadow->color.b, definition.shadow->color.a);
    source.SetShadowOffset(definition.shadow->offset.x, definition.shadow->offset.y);
  }

  if (definition.has_spacing) {
    source.SetSpacing(definition.spacing);

  }

  source.ApplyStyleFromMask(target, 0x1F);
  target->SetDirtyFlags(target->GetDirtyFlags() & ~0x1Fu);
}

}

void CSimpleFontString::OnRectChanged(const ScreenRect &oldRect) {
  InvalidateRelativeDependents();

  const auto &newRect = GetRect();

  auto &render = MutableRender();
  const bool offScreenStatusChanged =
      render.OnOwnerRectChanged(oldRect, newRect);

  if (offScreenStatusChanged) {
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
  }

  NotifyOwningScrollFrameOfContentChangeIfVisible();
}

void CSimpleFontString::OnParentAlphaChanged(bool forceRefresh) {
  uint8_t parentAlpha = 0xFF;
  if (const auto *parentFrame = dynamic_cast<const CSimpleFrame *>(GetParent())) {
    parentAlpha = parentFrame->GetCascadedAlphaByte();
  }

  auto &render = GetRender();
  render.RefreshInheritedAlpha(parentAlpha, forceRefresh);
  render.SyncEmbeddedTextureAlphaFromTextColor();
}

void CSimpleFontString::OnFrameScaleChanged(float effectiveScale, bool force) {
  auto &render = GetRender();
  if (!force &&
      std::fabs(render.GetScaleFactor() - effectiveScale) < kClientScaleEpsilon) {
    return;
  }

  render.SetScaleFactor(effectiveScale);

  if (!GetFontFile().empty() && render.GetFontHeight() != 0.0f) {
    if (!SetFont(GetFontFile().c_str(), render.GetFontHeight(),
                 GetFontFlagsBits(), true)) {
      MarkLayoutDirty();
    }
    return;
  }

  MarkLayoutDirty();
}

void CSimpleFontString::OnFrameDepthChanged(float effectiveDepth,
                                           bool force) {
  auto &render = GetRender();
  const float clampedDepth = std::max(effectiveDepth, kMinimumInheritedTextDepth);
  if (!force &&
      openwow::math::float_compare::WithinClientEpsilon(
          clampedDepth, render.GetTextGeometryInputs().depth)) {
    return;
  }

  render.SetTextDepth(clampedDepth);
  MarkLayoutDirty();
}

void CSimpleFontString::UpdateMeasuredStringMetrics() const noexcept {
  const auto measurement = MeasureLiveFontStringMetrics(*this);
  if (!measurement.has_value()) {
    const_cast<CSimpleFontString &>(*this).SetTruncated(false);
    return;
  }

  auto &mutable_self = const_cast<CSimpleFontString &>(*this);
  const float inverse_scale = 1.0f / EffectiveScale(mutable_self.GetRender());
  mutable_self.MutableRender().SetCachedDimensions(
      measurement->width * inverse_scale, measurement->height * inverse_scale);
  mutable_self.SetTruncated(measurement->truncated);
}

void CSimpleFontString::UpdateTextLayout() {
  auto &render = MutableRender();
  if (!render.IsLayoutValid()) {
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
    return;
  }

  render.ClearTextLayout();
  render.DestroyTextureNodes();

  const auto &rect = GetRect();
  const auto& face = render.GetFontFace();
  const std::string_view text = render.GetText();
  if (rect.Width() <= 0.0f || rect.Height() <= 0.0f || !face ||
      text.empty()) {
    QueueOwningFrameDrawLayerStateUpdateIfVisible();
    return;
  }

  const float scale = EffectiveScale(render);
  const float width = CScriptRegion::GetWidth() != 0.0f
                          ? CScriptRegion::GetWidth()
                          : render.GetCachedWidth();
  const float height = CScriptRegion::GetHeight() != 0.0f
                           ? CScriptRegion::GetHeight()
                           : render.GetCachedHeight();
  auto layout = openwow::render::text::LayoutText(
      *face, text,
      BuildLayoutRequest(*this, std::min(width, rect.Width() / scale),
                         std::min(height, rect.Height() / scale)));
  truncated_ = layout.truncated;
  const float rendered_height = layout.height / EffectiveScale(render);
  render.SetTextLayout(std::move(layout));
  render.CreateTextureNodes(rendered_height);

  QueueOwningFrameDrawLayerStateUpdateIfVisible();
}

float CSimpleFontString::GetStringWidth() const noexcept {
  if (render_.GetCachedWidth() == 0.0f) {
    UpdateMeasuredStringMetrics();
  }
  return render_.GetCachedWidth();
}

float CSimpleFontString::GetStringHeight() const noexcept {
  if (render_.GetCachedHeight() == 0.0f) {
    UpdateMeasuredStringMetrics();
  }
  return render_.GetCachedHeight();
}

std::vector<std::uint32_t> CSimpleFontString::CollectLineStartOffsets(
    const std::string_view text, const float max_width) const {
  const auto &render = GetRender();
  const auto& face = render.GetFontFace();
  if (!face || text.empty() || max_width <= 0.0f) return {};

  const auto layout = openwow::render::text::LayoutText(
      *face, text, BuildLayoutRequest(*this, max_width, 0.0f));
  std::vector<std::uint32_t> offsets;
  offsets.reserve(layout.lines.size());
  for (const auto& line : layout.lines) {
    offsets.push_back(static_cast<std::uint32_t>(line.begin));
  }
  return offsets;
}

bool CSimpleFontString::SetAlphaGradient(int start, int length) {
  alphaGradientStart_ = static_cast<uint16_t>(start);
  alphaGradientLength_ = static_cast<uint16_t>(length);
  MutableRender().Invalidate();
  return openwow::render::text::IsTextAlphaGradientActive(
      render_.GetText(), start, length);
}

bool CSimpleFontString::SetFont(const char *path, float height, std::uint32_t fontFlags,
                                bool forced) {
  auto &render = MutableRender();
  const auto& current_face = render.GetFontFace();
  if (!forced && path != nullptr && current_face &&
      openwow::text::EqualsIgnoreCaseAscii(path, current_face->path()) &&
      openwow::math::float_compare::WithinClientEpsilon(height, render.GetFontHeight()) &&
      fontFlags == GetFontFlagsBits()) {
    return true;
  }

  std::shared_ptr<openwow::render::text::FontFace> new_face;
  if (path != nullptr && height != 0.0f) {
    new_face = openwow::render::text::FontFace::LoadFile(
        path, FontPixelHeight(render, height), ResolveFontStyle(fontFlags));
    if (!new_face) return false;
  }

  render.SetFontHeight(height);
  SetFontSize(height);
  render.ClearTextLayout();
  render.SetFontFace(std::move(new_face));
  if (!render.GetFontFace()) {
    SetFontFile({});
    SetFontFlagsBits(0);
    return true;
  }

  SetFontFile(path);
  SetFontFlagsBits(fontFlags);
  render.Invalidate();
  return true;
}

void CSimpleFontString::OnSpacingChanged() noexcept {
  auto &render = MutableRender();
  render.SetCachedDimensions(render.GetCachedWidth(), 0.0f);
  if (render.GetTextLayout().has_value()) {
    render.ClearTextLayout();
    MarkLayoutDirty();
  }
}

void CSimpleFontString::SetTextHeight(float height) noexcept {
  auto &render = MutableRender();
  if (openwow::math::float_compare::WithinClientEpsilon(height,
                                                        render.GetFontHeight())) {
    return;
  }

  render.SetFontHeight(height);
  SetFontSize(height);
  render.SetJustifyFlags(render.GetJustifyFlags() & ~0x200u);
  if (!GetFontFile().empty()) {
    if (auto face = openwow::render::text::FontFace::LoadFile(
            GetFontFile(), FontPixelHeight(render, height),
            ResolveFontStyle(GetFontFlagsBits()))) {
      render.SetFontFace(std::move(face));
    }
  }
  render.Invalidate();
}

int CSimpleFontString::CountLeadingCharsWithinWidth(
    const char* text, int length, float width) const {
  const auto& render = GetRender();
  if (length == 0 && text != nullptr) {
    length = static_cast<int>(std::strlen(text));
  }
  const auto& face = render.GetFontFace();
  if (!face || text == nullptr || length <= 0 || width <= 0.0f) return 0;
  return openwow::render::text::CountLeadingVisibleElements(
      *face, std::string_view(text, static_cast<std::size_t>(length)),
      width * EffectiveScale(render));
}

int CSimpleFontString::CountTrailingCharsWithinWidth(
    const char* text, int length, float width) const {
  const auto& render = GetRender();
  if (length == 0 && text != nullptr) {
    length = static_cast<int>(std::strlen(text));
  }
  const auto& face = render.GetFontFace();
  if (!face || text == nullptr || length <= 0 || width <= 0.0f) return 0;
  return openwow::render::text::CountTrailingVisibleElements(
      *face, std::string_view(text, static_cast<std::size_t>(length)),
      width * EffectiveScale(render));
}

float CSimpleFontString::MeasureSubstringWidth(const char* text, int length) const {
  const auto& render = GetRender();
  if (length == 0 && text != nullptr) {
    length = static_cast<int>(std::strlen(text));
  }
  const auto& face = render.GetFontFace();
  if (!face || text == nullptr || length <= 0) return 0.0f;
  const auto layout = openwow::render::text::LayoutText(
      *face, std::string_view(text, static_cast<std::size_t>(length)), {});
  return layout.width / EffectiveScale(render);
}

void CSimpleFontString::LoadXML(const openwow::ui::xml::XMLFrameDef &frame_def,
                                openwow::ui::xml::ErrorContext *error_handler,
                                const openwow::ui::FontDefinitionRegistry *font_registry) {
  std::unordered_set<std::string> inheritance_stack;
  (void)LoadXMLWithInheritance(frame_def, error_handler, font_registry,
                               &inheritance_stack);
}

bool CSimpleFontString::LoadXMLWithInheritance(
    const openwow::ui::xml::XMLFrameDef &frame_def,
    openwow::ui::xml::ErrorContext *error_handler,
    const openwow::ui::FontDefinitionRegistry *font_registry,
    std::unordered_set<std::string> *inheritance_stack) {
  for (const auto &template_name : openwow::ui::framexml::SplitTemplateList(
           frame_def.inherits,
           openwow::ui::framexml::TemplateListSyntax::kCommaSeparated)) {
    if (font_registry != nullptr) {
      if (const auto inherited_font = font_registry->Get(template_name);
          inherited_font.has_value()) {
        ApplyResolvedFontDefinition(this, *inherited_font);
        continue;
      }
    }
    if (!inheritance_stack->insert(template_name).second) {
      if (error_handler != nullptr) {
        error_handler->ReportError("Recursively inherited node: %s",
                                   template_name.c_str());
      }
      return false;
    }
    const auto *template_def =
        openwow::ui::xml::FrameXMLParser::GetTemplate(template_name);
    if (template_def == nullptr) {
      inheritance_stack->erase(template_name);
      if (error_handler != nullptr) {
        error_handler->ReportError("Couldn't find inherited node: %s",
                                   template_name.c_str());
      }
      continue;
    }
    const bool inherited = LoadXMLWithInheritance(
        *template_def, error_handler, font_registry, inheritance_stack);
    inheritance_stack->erase(template_name);
    if (!inherited) return false;
  }

  LoadRegionLayoutFromXML(*this, frame_def, error_handler);

  if (const char *hidden = FindAttributeValue(frame_def, "hidden");
      hidden != nullptr && *hidden != '\0') {
    if (ScriptParseBoolStringOrDefault(hidden, false)) {
      Hide();
    } else {
      Show();
    }
  }

  if (!frame_def.text.empty()) {
    std::string localized =
        openwow::game::Localization::Get().GetString(frame_def.text, "");
    if (localized.empty()) {
      localized = frame_def.text;
    }
    MutableRender().SetText(localized.c_str(), true);
  }

  if (const char *non_space_wrap = FindAttributeValue(frame_def, "nonspacewrap");
      non_space_wrap != nullptr && *non_space_wrap != '\0') {
    SetNonSpaceWrap(ScriptParseBoolStringOrDefault(non_space_wrap, false));
  }

  if (const char *word_wrap = FindAttributeValue(frame_def, "wordwrap");
      word_wrap != nullptr && *word_wrap != '\0') {
    SetWordWrap(ScriptParseBoolStringOrDefault(word_wrap, false));
  }

  if (const char *bytes = FindAttributeValue(frame_def, "bytes");
      bytes != nullptr && *bytes != '\0') {
    AllocTextBuffer(static_cast<std::uint16_t>(
        openwow::core::ParseSignedDecimalLikeSub76F0D0(bytes)));
  }

  if (const char *max_lines = FindAttributeValue(frame_def, "maxLines");
      max_lines != nullptr && *max_lines != '\0') {
    SetMaxLines(openwow::core::ParseSignedDecimalLikeSub76F0D0(max_lines) & 0xFFFFFFu);
  }

  if (!frame_def.font.empty()) {
    bool resolved_named_font = false;
    if (font_registry != nullptr) {
      if (const auto named_font = font_registry->Get(frame_def.font);
          named_font.has_value()) {
        ApplyResolvedFontDefinition(this, *named_font);
        resolved_named_font = true;
      }
    }

    if (!resolved_named_font) {
      const float font_height = frame_def.font_height.value_or(0.0f);
      if (!(font_height > std::numeric_limits<float>::epsilon())) {
        if (error_handler != nullptr) {
          error_handler->ReportError(
              "FontString %s: invalid font height %f in %s element",
              FontStringDisplayName(*this), static_cast<double>(font_height),
              frame_def.type.empty() ? "FontString" : frame_def.type.c_str());
        }
        return false;
      }

      const char *outline = FindAttributeValue(frame_def, "outline");
      const bool monochrome =
          ScriptParseBoolStringOrDefault(FindAttributeValue(frame_def, "monochrome"),
                                         false);
      if (SetFont(frame_def.font.c_str(), font_height,
                  BuildXmlFontFlags(outline, monochrome), false)) {
        dirtyFlags_ &= ~0x1u;
      } else if (error_handler != nullptr) {
        error_handler->ReportError("FontString %s: Unable to load font file %s",
                                   FontStringDisplayName(*this), frame_def.font.c_str());
      }
    }
  }

  if (const char *spacing = FindAttributeValue(frame_def, "spacing");
      spacing != nullptr && *spacing != '\0') {
    SetSpacing(openwow::ui::PixelUiHorizontalCoordinateToStored(
        static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(spacing))));

  }

  if (const char *justify_v = FindAttributeValue(frame_def, "justifyV");
      justify_v != nullptr && *justify_v != '\0') {
    std::uint8_t bits = 0;
    if (TryParseJustifyBits(justify_v, false, &bits)) {
      SetJustifyV(bits == 8u ? JustifyV::Top
                             : bits == 32u ? JustifyV::Bottom
                                           : JustifyV::Middle);
      dirtyFlags_ &= ~0x2u;
    }
  }

  if (const char *justify_h = FindAttributeValue(frame_def, "justifyH");
      justify_h != nullptr && *justify_h != '\0') {
    std::uint8_t bits = 0;
    if (TryParseJustifyBits(justify_h, true, &bits)) {
      SetJustifyH(bits == 1u ? JustifyH::Left
                             : bits == 4u ? JustifyH::Right
                                           : JustifyH::Center);
      dirtyFlags_ &= ~0x2u;
    }
  }

  if (const char *indented = FindAttributeValue(frame_def, "indented");
      indented != nullptr && *indented != '\0') {
    SetIndentedWordWrap(ScriptParseBoolStringOrDefault(indented, false));
  }

  if (const char *alpha = FindAttributeValue(frame_def, "alpha");
      alpha != nullptr && *alpha != '\0') {
    const float clamped_alpha = std::clamp(
        static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(alpha)), 0.0f, 1.0f);
    auto &render = MutableRender();
    render.SetColorAlpha(static_cast<std::uint8_t>(clamped_alpha * 255.0f));
    render.SyncEmbeddedTextureAlphaFromTextColor();
  }

  if (frame_def.has_text_color) {
    SetTextColor(frame_def.text_color.r, frame_def.text_color.g,
                 frame_def.text_color.b, frame_def.text_color.a);
  }

  if (frame_def.has_shadow) {
    FontShadow shadow;
    shadow.offsetX = frame_def.shadow.x;
    shadow.offsetY = frame_def.shadow.y;
    shadow.r = frame_def.shadow.r;
    shadow.g = frame_def.shadow.g;
    shadow.b = frame_def.shadow.b;
    shadow.a = frame_def.shadow.a;
    ApplyShadowStyle(shadow);
  }

  ApplyDefaultParentAnchor();
  return true;
}

void CSimpleFontString::ApplyDefaultParentAnchor() noexcept {
  if (GetNumPoints() != 0) {
    return;
  }

  auto *parent_region = GetParent();
  if (parent_region == nullptr) {
    return;
  }

  RegionAnchor anchor;
  switch (GetJustifyH()) {
  case JustifyH::Left:
    anchor.point = FramePoint::Left;
    anchor.relativePoint = FramePoint::Left;
    break;
  case JustifyH::Right:
    anchor.point = FramePoint::Right;
    anchor.relativePoint = FramePoint::Right;
    break;
  case JustifyH::Center:
  default:
    anchor.point = FramePoint::Center;
    anchor.relativePoint = FramePoint::Center;
    break;
  }
  anchor.relativeTo = parent_region;
  anchor.offsetX = 0.0f;
  anchor.offsetY = 0.0f;
  SetPoint(anchor);
}

void CSimpleFontString::OnParentFontHeightChanged(float , bool ) {
}

}

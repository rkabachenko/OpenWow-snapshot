#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/texture_natural_size.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/widgets/simple_html.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/text/utf8.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

constexpr const char* kSimpleHtmlGeneratedContentSuffix = ".__HTMLContent1";

struct SimpleHtmlDisplayText {
  std::string text;
  std::string justify_h{"LEFT"};
  float color_r{1.0F};
  float color_g{1.0F};
  float color_b{1.0F};
  float color_a{1.0F};
};

std::string SimpleHtmlGeneratedContentName(const std::string& parent) {
  return parent + kSimpleHtmlGeneratedContentSuffix;
}

std::string NormalizeSimpleHtmlJustify(std::string align) {
  std::transform(align.begin(), align.end(), align.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (align == "CENTER" || align == "RIGHT") {
    return align;
  }
  return "LEFT";
}

SimpleHtmlDisplayText BuildSimpleHtmlDisplayText(const std::string& html) {
  openwow::ui::widgets::SimpleHTML parser;
  parser.SetText(html);

  SimpleHtmlDisplayText out;
  bool have_text = false;
  bool have_style = false;
  for (const auto& block : parser.GetParsedBlocks()) {
    if (block.text.empty()) {
      continue;
    }
    if (have_text && block.line_break) {
      out.text.push_back('\n');
    }
    out.text += block.text;
    have_text = true;
    if (!have_style) {
      out.justify_h = NormalizeSimpleHtmlJustify(block.align);
      out.color_r = block.r;
      out.color_g = block.g;
      out.color_b = block.b;
      out.color_a = block.a;
      have_style = true;
    }
  }
  return out;
}

std::string MaskPasswordText(std::string_view text) {

  return std::string(text.size(), '*');
}

bool IsCursorInsideEditBoxHyperlink(std::string_view text, int cursor_byte) {
  const std::size_t cursor = cursor_byte <= 0
                                 ? 0
                                 : std::min(static_cast<std::size_t>(cursor_byte),
                                            text.size());
  bool inside = false;
  std::size_t offset = 0;
  while (offset < cursor) {
    const auto element = ClassifyWowTextElement(text, offset);
    if (element.next_offset <= offset) {
      break;
    }
    if (element.type == WowTextEscapeType::HyperlinkStart) {
      inside = true;
    } else if (element.type == WowTextEscapeType::HyperlinkEnd &&
               element.next_offset <= cursor) {
      inside = false;
    }
    offset = element.next_offset;
  }
  return inside;
}

std::uint8_t QuantizeAlphaByteTruncated(float value) {
  const float clamped = std::clamp(value, 0.0F, 1.0F);
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(clamped * 255.0F), 0, 255));
}

float NormalizeAlphaByte(std::uint8_t value) {
  return static_cast<float>(value) / 255.0F;
}

std::uint8_t MultiplyAlphaBytes(std::uint8_t lhs, std::uint8_t rhs) {
  return static_cast<std::uint8_t>(
      (static_cast<int>(lhs) * static_cast<int>(rhs)) / 255);
}

}

void GlueWidgetRuntime::SyncSimpleHtmlContent(const std::string& name) {
  auto parent_it = widgets_.find(name);
  if (parent_it == widgets_.end() || ToLowerAscii(parent_it->second.kind) != "simplehtml") {
    return;
  }

  const auto display = BuildSimpleHtmlDisplayText(parent_it->second.text);
  const std::string content_name = SimpleHtmlGeneratedContentName(name);

  if (display.text.empty()) {
    const auto existing = widgets_.find(content_name);
    const bool removed = existing != widgets_.end();
    const std::string old_parent = removed ? existing->second.parent : std::string();
    const std::string old_inherits = removed ? existing->second.inherits : std::string();
    if (removed) {
      widgets_.erase(existing);
      widget_names_lower_.erase(ToLowerAscii(content_name));
      ReindexVisibilityRelationships(content_name, old_parent, old_inherits);
      ForgetFontStringMetrics(content_name);
    }
    layout_frames_by_name_.erase(content_name);
    layout_dirty_ = true;
    deferred_hit_test_refresh_ = true;
    if (removed) {
      ++visibility_revision_;
      MarkVisibleWidgetsDirty();
    }
    return;
  }

  const GlueWidgetState* style_source = nullptr;
  std::string style_source_name;
  if (const auto children = children_by_parent_.find(name);
      children != children_by_parent_.end()) {
    for (const auto& child_name : children->second) {
      const auto child = widgets_.find(child_name);
      if (child == widgets_.end() || child_name == content_name ||
          ToLowerAscii(child->second.kind) != "fontstring") {
        continue;
      }
      style_source = &child->second;
      style_source_name = child_name;
      break;
    }
  }

  GlueWidgetState content;
  if (auto existing = widgets_.find(content_name); existing != widgets_.end()) {
    content = existing->second;
  } else if (style_source != nullptr) {
    content = *style_source;
  }

  content.name = content_name;
  content.kind = "FontString";
  content.parent = name;
  content.inherits = style_source != nullptr ? style_source->inherits : parent_it->second.inherits;
  content.font_style =
      style_source != nullptr ? style_source->font_style : parent_it->second.font_style;
  content.justify_h = display.justify_h;
  content.justify_v = "TOP";
  content.draw_layer = "OVERLAY";
  content.draw_sublevel = 0;
  content.frame_strata = parent_it->second.frame_strata;
  content.frame_level = parent_it->second.frame_level;

  content.x = parent_it->second.x;
  content.y = parent_it->second.y;
  content.width = parent_it->second.width;
  content.height = 0;
  content.text = display.text;
  content.color_r = display.color_r;
  content.color_g = display.color_g;
  content.color_b = display.color_b;
  content.color_a = display.color_a;
  content.visible = true;
  content.virtual_template = false;
  content.scroll_child_content = parent_it->second.scroll_child_content;
  content.word_wrap = true;
  content.non_space_wrap = false;
  content.indented_word_wrap = false;

  const bool content_is_new = !widgets_.contains(content_name);
  const auto old_content = widgets_.find(content_name);
  const std::string old_parent =
      old_content != widgets_.end() ? old_content->second.parent : std::string();
  const std::string old_inherits =
      old_content != widgets_.end() ? old_content->second.inherits : std::string();
  widgets_.insert_or_assign(content_name, content);
  widget_names_lower_.insert(ToLowerAscii(content_name));
  ReindexVisibilityRelationships(content_name, old_parent, old_inherits);
  MarkFontStringMetricsDirty(content_name);
  if (content_is_new) {
    if (std::find(widget_registration_order_.begin(), widget_registration_order_.end(),
                  content_name) == widget_registration_order_.end()) {
      widget_registration_order_.push_back(content_name);
    }
    ++visibility_revision_;

    MarkVisibleWidgetsDirty();
  }

  openwow::ui::framexml::UiFrame frame;
  if (auto existing_frame = layout_frames_by_name_.find(content_name);
      existing_frame != layout_frames_by_name_.end()) {
    frame = existing_frame->second;
  } else if (!style_source_name.empty()) {
    if (auto style_frame = layout_frames_by_name_.find(style_source_name);
        style_frame != layout_frames_by_name_.end()) {
      frame = style_frame->second;
    }
  }
  frame.name = content_name;
  frame.kind = "FontString";
  frame.parent = name;
  frame.inherits = content.inherits;
  frame.font_style = content.font_style;
  frame.justify_h = content.justify_h;
  frame.justify_v = content.justify_v;
  frame.text_spacing_stored = content.text_spacing_stored;
  frame.text_height_stored = content.text_height_stored;
  frame.max_lines = content.max_lines;
  frame.draw_layer = content.draw_layer;
  frame.draw_sublevel = content.draw_sublevel;
  frame.frame_strata = content.frame_strata;
  frame.frame_level = content.frame_level;

  frame.width.reset();
  frame.height.reset();
  frame.text = content.text;
  frame.scroll_child_content = content.scroll_child_content;
  frame.color_r = content.color_r;
  frame.color_g = content.color_g;
  frame.color_b = content.color_b;
  frame.color_a = content.color_a;
  frame.visible = true;
  frame.virtual_template = false;
  frame.set_all_points = false;
  frame.anchors = {
      openwow::ui::framexml::UiAnchor{
          .point = "TOPLEFT",
          .relative_to = name,
          .relative_point = "TOPLEFT",
          .x = 0.0F,
          .y = 0.0F,
      },
      openwow::ui::framexml::UiAnchor{
          .point = "TOPRIGHT",
          .relative_to = name,
          .relative_point = "TOPRIGHT",
          .x = 0.0F,
          .y = 0.0F,
      },
  };
  layout_frames_by_name_.insert_or_assign(content_name, std::move(frame));
  layout_dirty_ = true;
  deferred_hit_test_refresh_ = true;
}

void GlueWidgetRuntime::SetAlpha(const std::string& name, float alpha) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetAlpha: unknown widget: " + name);
    return;
  }
  const auto alpha_byte = QuantizeAlphaByteTruncated(alpha);
  if (it->second.alpha_byte == alpha_byte) {
    return;
  }
  it->second.alpha_byte = alpha_byte;
  it->second.alpha = NormalizeAlphaByte(alpha_byte);
}

float GlueWidgetRuntime::EffectiveAlpha(const std::string& name) const {
  if (name.empty()) {
    return 1.0F;
  }
  std::vector<std::string> chain;
  std::unordered_set<std::string> visited;
  std::string current = name;
  while (!current.empty()) {
    if (!visited.insert(current).second) {
      break;
    }
    const auto widget_it = widgets_.find(current);
    if (widget_it == widgets_.end()) {
      break;
    }
    chain.push_back(current);
    current = widget_it->second.parent;
  }

  std::uint8_t out = 0xFF;
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const auto widget_it = widgets_.find(*it);
    if (widget_it == widgets_.end()) {
      continue;
    }
    out = MultiplyAlphaBytes(out, widget_it->second.alpha_byte);
    if (const auto* props = FindProps(*it); props != nullptr) {
      if (props->animation_alpha.has_value()) {
        out = MultiplyAlphaBytes(
            out, QuantizeAlphaByteTruncated(*props->animation_alpha));
      }
      const int changed = static_cast<int>(out) + static_cast<int>(
          std::trunc(props->animation_alpha_change * 255.0f));
      out = static_cast<std::uint8_t>(std::clamp(changed, 0, 255));
    }
    if (out == 0) {
      return 0.0F;
    }
  }
  return NormalizeAlphaByte(out);
}

void GlueWidgetRuntime::SetTexture(const std::string& name, const std::string& texture_file) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetTexture: unknown widget: " + name);
    return;
  }
  it->second.texture_file = texture_file;

  if (texture_natural_size_source_ != nullptr && !texture_file.empty()) {
    texture_natural_size_source_->QueueTextureLoad(texture_file);
  }
}

void GlueWidgetRuntime::SetTexCoord(const std::string& name,
                                    float left,
                                    float right,
                                    float top,
                                    float bottom) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetTexCoord: unknown widget: " + name);
    return;
  }
  it->second.tex_left = left;
  it->second.tex_right = right;
  it->second.tex_top = top;
  it->second.tex_bottom = bottom;
  it->second.tex_coords =
      openwow::ui::framexml::UiTextureCoordQuad::FromRect(left, right, top, bottom);
}

void GlueWidgetRuntime::SetTexCoordQuad(
    const std::string& name,
    const openwow::ui::framexml::UiTextureCoordQuad& tex_coords) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetTexCoordQuad: unknown widget: " + name);
    return;
  }
  it->second.tex_coords = tex_coords;
  it->second.tex_left = tex_coords.upper_left.u;
  it->second.tex_right = tex_coords.upper_right.u;
  it->second.tex_top = tex_coords.upper_left.v;
  it->second.tex_bottom = tex_coords.lower_left.v;
}

void GlueWidgetRuntime::SetVertexColor(const std::string& name, float r, float g, float b, float a) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetVertexColor: unknown widget: " + name);
    return;
  }
  it->second.color_r = r;
  it->second.color_g = g;
  it->second.color_b = b;
  it->second.color_a = a;
  it->second.has_vertex_color = true;
}

void GlueWidgetRuntime::SetShadowColor(const std::string& name,
                                       float r,
                                       float g,
                                       float b,
                                       float a) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetShadowColor: unknown widget: " + name);
    return;
  }
  it->second.shadow_r = r;
  it->second.shadow_g = g;
  it->second.shadow_b = b;
  it->second.shadow_a = a;
  it->second.has_shadow_color = true;
}

void GlueWidgetRuntime::SetShadowOffset(const std::string& name, float x, float y) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetShadowOffset: unknown widget: " + name);
    return;
  }
  it->second.shadow_x = x;
  it->second.shadow_y = y;
  it->second.has_shadow_offset = true;
}

void GlueWidgetRuntime::SetCapabilityAvailable(const std::string& name,
                                               const bool available) {
  if (available) {
    capability_unavailable_widgets_.erase(name);
    return;
  }
  capability_unavailable_widgets_.insert(name);
  SetEnabled(name, false);
}

bool GlueWidgetRuntime::IsCapabilityAvailable(const std::string& name) const {
  return !capability_unavailable_widgets_.contains(name);
}

void GlueWidgetRuntime::SetEnabled(const std::string& name, bool enabled) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetEnabled: unknown widget: " + name);
    return;
  }
  const bool effective_enabled = enabled && IsCapabilityAvailable(name);
  if (it->second.enabled == effective_enabled) {
    return;
  }
  it->second.enabled = effective_enabled;
  deferred_hit_test_refresh_ = true;
}

bool GlueWidgetRuntime::IsEnabled(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return false;
  }
  return it->second.enabled;
}

std::string GlueWidgetRuntime::EnsureButtonTextRegion(
    const std::string& name) {
  if (const auto bound = owned_text_region_by_widget_.find(name);
      bound != owned_text_region_by_widget_.end()) {
    return bound->second;
  }

  const auto owner_it = widgets_.find(name);
  if (owner_it == widgets_.end()) {
    return {};
  }
  const auto kind = ToLowerAscii(owner_it->second.kind);
  if (kind != "button" && kind != "checkbutton") {
    return {};
  }

  const GlueWidgetState owner = owner_it->second;
  const std::string text_key = name + ".__ButtonText";
  openwow::ui::framexml::UiFrame frame;
  frame.kind = "FontString";
  frame.name = text_key;
  frame.publish_to_lua = false;
  frame.region_role =
      openwow::ui::framexml::UiFrame::RegionRole::ButtonText;
  frame.parent = name;
  frame.font_style = owner.button_normal_font_style;
  frame.draw_layer = "OVERLAY";
  frame.frame_strata = owner.frame_strata;
  frame.frame_level = owner.frame_level;
  frame.anchors = {
      openwow::ui::framexml::UiAnchor{
          .point = "CENTER",
          .relative_to = name,
          .relative_point = "CENTER",
      },
  };
  if (owner.button_normal_color.has_value()) {
    frame.color_r = owner.button_normal_color->r;
    frame.color_g = owner.button_normal_color->g;
    frame.color_b = owner.button_normal_color->b;
    frame.color_a = owner.button_normal_color->a;
    frame.has_vertex_color = true;
  }
  layout_frames_by_name_.insert_or_assign(text_key, frame);

  RegisterWidget({
      .name = text_key,
      .kind = "FontString",
      .parent = name,
      .font_style = owner.button_normal_font_style,
      .draw_layer = "OVERLAY",
      .frame_strata = owner.frame_strata,
      .frame_level = owner.frame_level,
      .color_r = frame.color_r,
      .color_g = frame.color_g,
      .color_b = frame.color_b,
      .color_a = frame.color_a,
      .has_vertex_color = frame.has_vertex_color,
      .visible = true,
      .publish_to_lua = false,
      .region_role =
          openwow::ui::framexml::UiFrame::RegionRole::ButtonText,
  });
  return text_key;
}

void GlueWidgetRuntime::SetText(const std::string& name, const std::string& text) {
  auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueWidgetRuntime.SetText: unknown widget: " + name);
    return;
  }
  const auto lower_kind = ToLowerAscii(it->second.kind);
  const bool has_internal_text_region =
      lower_kind == "button" || lower_kind == "checkbutton" ||
      lower_kind == "editbox";
  std::string region_name =
      has_internal_text_region ? TextRegionForWidget(name) : std::string();
  if (region_name.empty() && !text.empty() &&
      (lower_kind == "button" || lower_kind == "checkbutton")) {
    region_name = EnsureButtonTextRegion(name);

    it = widgets_.find(name);
    if (it == widgets_.end()) {
      return;
    }
  }
  std::string masked_text;
  const std::string* rendered_text = &text;
  if (lower_kind == "editbox" && it->second.password) {
    masked_text = MaskPasswordText(text);
    rendered_text = &masked_text;
  }
  const bool owns_plain_render_text =
      lower_kind == "fontstring" || has_internal_text_region;

  const bool rendered_text_matches =
      region_name.empty() || widgets_.at(region_name).text == *rendered_text;
  if (owns_plain_render_text && it->second.text == text &&
      rendered_text_matches) {
    return;
  }

  it->second.text = text;
  if (lower_kind == "fontstring") {
    MarkFontStringMetricsDirty(name);
  }
  if (lower_kind == "editbox") {
    if (auto* props = GetProps(name); props != nullptr) {
      const int max_pos = static_cast<int>(it->second.text.size());
      props->edit_cursor_byte = std::clamp(props->edit_cursor_byte, 0, std::max(0, max_pos));
      if (props->edit_sel_start_byte >= 0 && props->edit_sel_end_byte >= 0) {
        const int s = std::clamp(props->edit_sel_start_byte, 0, std::max(0, max_pos));
        const int e = std::clamp(props->edit_sel_end_byte, 0, std::max(0, max_pos));
        if (s == e) {
          props->edit_sel_start_byte = -1;
          props->edit_sel_end_byte = -1;
        } else {
          props->edit_sel_start_byte = std::min(s, e);
          props->edit_sel_end_byte = std::max(s, e);
        }
      }

      props->cursor_changed = true;
      props->blink_accumulator = 0.0f;
      props->caret_visible = true;
    }
  }

  if (lower_kind == "simplehtml") {
    SyncSimpleHtmlContent(name);
  }
  if (!region_name.empty()) {
    auto& region = widgets_.at(region_name);
    if (region.text != *rendered_text) {
      region.text = *rendered_text;
      MarkFontStringMetricsDirty(region_name);
    }
  }

  if (lower_kind != "button" && lower_kind != "checkbutton" &&
      lower_kind != "editbox" && lower_kind != "fontstring") {
    layout_dirty_ = true;
  }
}

GlueEditBoxInsertion GlueWidgetRuntime::BuildEditBoxInsertion(
    const std::string& name, const std::string_view inserted_text) const {
  std::string next = GetText(name);
  int cursor = openwow::text::ClampUtf8ByteIndex(
      next, GetEditCursorByte(name));
  if (IsCursorInsideEditBoxHyperlink(next, cursor)) {
    return {};
  }

  const auto [selection_start, selection_end] =
      GetEditSelectionBytes(name);
  if (selection_start >= 0 && selection_end >= 0 &&
      selection_start != selection_end) {
    const int start = openwow::text::ClampUtf8ByteIndex(
        next, std::min(selection_start, selection_end));
    const int end = openwow::text::ClampUtf8ByteIndex(
        next, std::max(selection_start, selection_end));
    if (end > start) {
      next.erase(static_cast<std::size_t>(start),
                 static_cast<std::size_t>(end - start));
      cursor = start;
    }
  }

  next.insert(static_cast<std::size_t>(cursor), inserted_text);
  cursor += static_cast<int>(inserted_text.size());

  const int max_bytes = GetMaxBytes(name);
  if (max_bytes >= 0 && next.size() > static_cast<std::size_t>(max_bytes)) {
    std::size_t end = static_cast<std::size_t>(max_bytes);
    while (end > 0 &&
           (static_cast<unsigned char>(next[end]) & 0xC0u) == 0x80u) {
      --end;
    }
    next.resize(end);
    cursor = std::min(cursor, static_cast<int>(next.size()));
  }

  const int max_letters = GetMaxLetters(name);
  if (max_letters > 0 && CountEditBoxVisibleLetters(next) > max_letters) {
    const std::string_view view(next);
    std::size_t end = 0;
    int letters = 0;
    while (end < view.size() && letters < max_letters) {
      bool visible = false;
      end = AdvanceWowTextElement(view, end, &visible);
      letters += visible ? 1 : 0;
    }
    while (end < view.size()) {
      bool visible = false;
      const std::size_t following =
          AdvanceWowTextElement(view, end, &visible);
      if (visible) break;
      end = following;
    }
    next.resize(end);
    cursor = std::min(cursor, static_cast<int>(next.size()));
  }

  return {.accepted = true, .text = std::move(next), .cursor_byte = cursor};
}

std::string GlueWidgetRuntime::GetText(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return {};
  }
  const auto lower_kind = ToLowerAscii(it->second.kind);
  if (lower_kind == "editbox") {

    return it->second.text;
  }
  if (lower_kind == "button" || lower_kind == "checkbutton") {
    const std::string region_name = TextRegionForWidget(name);
    if (!region_name.empty()) {
      return widgets_.at(region_name).text;
    }
  }
  return it->second.text;
}

void GlueWidgetRuntime::SetEditInputLanguageToken(const std::string& name,
                                                  const std::string& token) {
  if (name.empty()) return;
  if (auto* props = GetProps(name); props != nullptr) {
    props->edit_input_language = token.empty() ? "ROMAN" : token;
  }
}

std::string GlueWidgetRuntime::GetEditInputLanguageToken(const std::string& name) const {
  if (name.empty()) return "ROMAN";
  if (const auto* props = FindProps(name);
      props != nullptr && !props->edit_input_language.empty()) {
    return props->edit_input_language;
  }
  return "ROMAN";
}

void GlueWidgetRuntime::SetEditCursorByte(const std::string& name, int byte_index) {
  if (name.empty()) return;
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return;
  if (ToLowerAscii(it->second.kind) != "editbox") return;
  if (auto* props = GetProps(name); props != nullptr) {
    const int max_pos = static_cast<int>(it->second.text.size());
    const int clamped = std::clamp(byte_index, 0, std::max(0, max_pos));
    if (clamped != props->edit_cursor_byte) {
      props->edit_cursor_byte = clamped;

      props->cursor_changed = true;
      props->blink_accumulator = 0.0f;
      props->caret_visible = true;
    }
  }
}

int GlueWidgetRuntime::GetEditCursorByte(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->edit_cursor_byte;
  }
  return 0;
}

void GlueWidgetRuntime::SetEditSelectionBytes(const std::string& name, int start_byte, int end_byte) {
  if (name.empty()) return;
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return;
  if (ToLowerAscii(it->second.kind) != "editbox") return;
  if (auto* props = GetProps(name); props != nullptr) {
    const int max_pos = static_cast<int>(it->second.text.size());
    const int s = std::clamp(start_byte, 0, std::max(0, max_pos));
    const int e = std::clamp(end_byte, 0, std::max(0, max_pos));
    if (s == e) {
      props->edit_sel_start_byte = -1;
      props->edit_sel_end_byte = -1;
      props->edit_cursor_byte = s;

      props->cursor_changed = true;
      props->blink_accumulator = 0.0f;
      props->caret_visible = true;
      return;
    }
    const int new_start = std::min(s, e);
    const int new_end = std::max(s, e);
    if (new_start != props->edit_sel_start_byte || new_end != props->edit_sel_end_byte) {
      props->edit_sel_start_byte = new_start;
      props->edit_sel_end_byte = new_end;
      props->edit_cursor_byte = new_end;

      props->cursor_changed = true;
      props->blink_accumulator = 0.0f;
      props->caret_visible = true;
    }
  }
}

std::pair<int, int> GlueWidgetRuntime::GetEditSelectionBytes(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    if (props->edit_sel_start_byte >= 0 && props->edit_sel_end_byte >= 0
        && props->edit_sel_start_byte != props->edit_sel_end_byte) {
      return {props->edit_sel_start_byte, props->edit_sel_end_byte};
    }
  }
  return {-1, -1};
}

void GlueWidgetRuntime::ClearEditSelection(const std::string& name) {
  if (auto* props = GetProps(name); props != nullptr) {
    if (props->edit_sel_start_byte >= 0 || props->edit_sel_end_byte >= 0) {
      props->edit_sel_start_byte = -1;
      props->edit_sel_end_byte = -1;

      props->cursor_changed = true;
      props->blink_accumulator = 0.0f;
      props->caret_visible = true;
    }
  }
}

int GlueWidgetRuntime::GetEditVisibleStartCodepoints(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->edit_visible_start_codepoints;
  }
  return 0;
}

void GlueWidgetRuntime::SetEditVisibleStartCodepoints(const std::string& name,
                                                      int visible_codepoints) {
  if (auto* props = GetProps(name); props != nullptr) {
    props->edit_visible_start_codepoints = std::max(0, visible_codepoints);
  }
}

int GlueWidgetRuntime::GetEditScrollOffsetPx(const std::string& name) const {
  if (const auto* props = FindProps(name); props != nullptr) {
    return props->edit_scroll_offset_px;
  }
  return 0;
}

void GlueWidgetRuntime::SetEditScrollOffsetPx(const std::string& name, int offset) {
  if (auto* props = GetProps(name); props != nullptr) {
    props->edit_scroll_offset_px = std::max(0, offset);
  }
}

void GlueWidgetRuntime::MarkCursorDirty(const std::string& name) {
  if (auto* props = GetProps(name); props != nullptr) {
    props->cursor_changed = true;
    props->blink_accumulator = 0.0f;
    props->caret_visible = true;
  }
}

std::string GlueWidgetRuntime::TextRegionForWidget(const std::string& name) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return {};
  const auto lower_kind = ToLowerAscii(it->second.kind);
  if (lower_kind != "button" && lower_kind != "checkbutton" && lower_kind != "editbox") {
    return {};
  }

  const auto binding = owned_text_region_by_widget_.find(name);
  if (binding == owned_text_region_by_widget_.end()) {
    return {};
  }
  const auto region = widgets_.find(binding->second);
  if (region == widgets_.end() ||
      ToLowerAscii(region->second.kind) != "fontstring" ||
      region->second.parent != name) {
    return {};
  }
  const bool role_matches_owner =
      (lower_kind == "editbox" &&
       region->second.region_role ==
           openwow::ui::framexml::UiFrame::RegionRole::EditBoxText) ||
      ((lower_kind == "button" || lower_kind == "checkbutton") &&
       region->second.region_role ==
           openwow::ui::framexml::UiFrame::RegionRole::ButtonText);
  if (!role_matches_owner) {
    return {};
  }
  return binding->second;
}

void GlueWidgetRuntime::SetJustifyH(const std::string& name, const std::string& justify_h) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.justify_h = justify_h;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.justify_h = it->second.justify_h;
  }
}

void GlueWidgetRuntime::SetJustifyV(const std::string& name, const std::string& justify_v) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  it->second.justify_v = justify_v;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.justify_v = it->second.justify_v;
  }
}

void GlueWidgetRuntime::SetFontStyle(const std::string& name, const std::string& font_style) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  const std::string normalized = Trim(font_style);
  if (it->second.font_style == normalized) {
    return;
  }
  it->second.font_style = normalized;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.font_style = it->second.font_style;
  }
  if (ToLowerAscii(it->second.kind) == "fontstring") {
    MarkFontStringMetricsDirty(name);
  }
}

void GlueWidgetRuntime::SetButtonFontStyle(const std::string& name,
                                           const GlueButtonFontState state,
                                           const std::string& font_style) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }

  std::string* target = nullptr;
  switch (state) {
    case GlueButtonFontState::kNormal:
      target = &it->second.button_normal_font_style;
      break;
    case GlueButtonFontState::kDisabled:
      target = &it->second.button_disabled_font_style;
      break;
    case GlueButtonFontState::kHighlight:
      target = &it->second.button_highlight_font_style;
      break;
  }

  if (target == nullptr) {
    return;
  }

  *target = Trim(font_style);
  if (state == GlueButtonFontState::kNormal) {
    SetFontStyle(name, *target);
  }
}

std::string GlueWidgetRuntime::GetButtonFontStyle(const std::string& name,
                                                  const GlueButtonFontState state) const {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return {};
  }

  switch (state) {
    case GlueButtonFontState::kNormal:
      return it->second.button_normal_font_style;
    case GlueButtonFontState::kDisabled:
      return it->second.button_disabled_font_style;
    case GlueButtonFontState::kHighlight:
      return it->second.button_highlight_font_style;
  }
  return {};
}

void GlueWidgetRuntime::SetTextSpacing(const std::string& name, float stored_spacing) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  if (it->second.text_spacing_stored == stored_spacing) {
    return;
  }
  it->second.text_spacing_stored = stored_spacing;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.text_spacing_stored = stored_spacing;
  }
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::SetTextHeightStored(const std::string& name,
                                            float stored_height) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  if (it->second.text_height_stored == stored_height) {
    return;
  }
  it->second.text_height_stored = stored_height;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.text_height_stored = stored_height;
  }
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::SetMaxLines(const std::string& name, int max_lines) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) {
    return;
  }
  const int normalized = std::max(0, max_lines);
  if (it->second.max_lines == normalized) {
    return;
  }
  it->second.max_lines = normalized;
  if (auto lf = layout_frames_by_name_.find(name); lf != layout_frames_by_name_.end()) {
    lf->second.max_lines = it->second.max_lines;
  }
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::SetWordWrap(const std::string& name, bool enable) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return;
  if (it->second.word_wrap == enable) return;
  it->second.word_wrap = enable;
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::SetNonSpaceWrap(const std::string& name, bool enable) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return;
  if (it->second.non_space_wrap == enable) return;
  it->second.non_space_wrap = enable;
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::SetIndentedWordWrap(const std::string& name, bool enable) {
  const auto it = widgets_.find(name);
  if (it == widgets_.end()) return;
  if (it->second.indented_word_wrap == enable) return;
  it->second.indented_word_wrap = enable;
  MarkFontStringMetricsDirty(name);
}

void GlueWidgetRuntime::UpdateCaretBlink(const std::string& name, float dt_seconds) {
  auto* props = GetProps(name);
  if (props == nullptr) return;

  constexpr float kBlinkInterval = 0.53f;
  props->blink_accumulator += dt_seconds;
  while (props->blink_accumulator >= kBlinkInterval) {
    props->blink_accumulator -= kBlinkInterval;
    props->caret_visible = !props->caret_visible;
  }
}

bool GlueWidgetRuntime::IsCaretVisible(const std::string& name) const {
  const auto* props = FindProps(name);
  return props != nullptr ? props->caret_visible : true;
}

bool GlueWidgetRuntime::ConsumeCursorChanged(const std::string& name) {
  auto* props = GetProps(name);
  if (props == nullptr) return false;
  const bool was_changed = props->cursor_changed;
  props->cursor_changed = false;
  return was_changed;
}

void GlueWidgetRuntime::QueueCursorChangedEvent(
    const std::string& name, float x, float y, float w, float h) {
  pending_cursor_events_.push_back({name, x, y, w, h});
}

std::vector<GlueWidgetRuntime::PendingCursorChangedEvent>
GlueWidgetRuntime::ConsumeCursorChangedEvents() {
  std::vector<PendingCursorChangedEvent> result;
  result.swap(pending_cursor_events_);
  return result;
}

void GlueWidgetRuntime::QueueAnimationFinishedEvent(const std::string& name) {
  if (!name.empty()) {
    pending_animation_finished_events_.push_back({name});
  }
}

std::vector<GlueWidgetRuntime::PendingAnimationFinishedEvent>
GlueWidgetRuntime::ConsumeAnimationFinishedEvents() {
  std::vector<PendingAnimationFinishedEvent> result;
  result.swap(pending_animation_finished_events_);
  return result;
}

}

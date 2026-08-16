#include "openwow/ui/glue/glue_font_registry.h"

#include "openwow/ui/framexml_font_registry.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cmath>

namespace openwow::ui::glue {

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

namespace {

std::string NormalizeFontPath(std::string value) {
  std::replace(value.begin(), value.end(), '\\', '/');
  value = Trim(value);
  if (value.empty()) return {};
  std::string collapsed;
  collapsed.reserve(value.size());
  char previous = '\0';
  for (const char ch : value) {
    if (ch == '/' && previous == '/') {
      continue;
    }
    collapsed.push_back(ch);
    previous = ch;
  }
  value = std::move(collapsed);
  if (value.front() != '/') {
    value.insert(value.begin(), '/');
  }
  return value;
}

bool LooksLikeFontAssetPath(const std::string& value) {
  const std::string lower = ToLowerAscii(Trim(value));
  return lower.find('/') != std::string::npos || lower.find('\\') != std::string::npos ||
         lower.ends_with(".ttf") || lower.ends_with(".otf");
}

GlueFontStyle ToGlueFontStyle(const openwow::ui::FontDefinition& def) {
  GlueFontStyle style;
  style.name = Trim(def.name);
  style.inherits = Trim(def.inherits);
  style.font_file = LooksLikeFontAssetPath(def.font_file)
                        ? NormalizeFontPath(def.font_file)
                        : Trim(def.font_file);
  style.has_font_file = !style.font_file.empty();
  if (def.has_height) {
    style.height_px = static_cast<int>(
        std::lround(openwow::ui::StoredUiHorizontalCoordinateToPixels(def.height)));
    style.has_height = style.height_px > 0;
  }
  if (def.has_color) {
    style.color_r = def.color.r;
    style.color_g = def.color.g;
    style.color_b = def.color.b;
    style.color_a = def.color.a;
    style.has_color = true;
  }
  if (def.shadow.has_value()) {
    style.shadow_x_px =
        openwow::ui::StoredUiHorizontalCoordinateToPixels(def.shadow->offset.x);
    style.shadow_y_px =
        openwow::ui::StoredUiHorizontalCoordinateToPixels(def.shadow->offset.y);
    style.shadow_r = def.shadow->color.r;
    style.shadow_g = def.shadow->color.g;
    style.shadow_b = def.shadow->color.b;
    style.shadow_a = def.shadow->color.a;
    style.has_shadow = true;
  }
  if (def.has_spacing) {
    style.spacing_px = openwow::ui::StoredUiHorizontalCoordinateToPixels(def.spacing);
    style.has_spacing = true;
  }
  style.justify_h = Trim(def.justify_h);
  style.justify_v = Trim(def.justify_v);
  style.outline = Trim(def.outline);
  style.monochrome = def.monochrome;
  style.non_space_wrap = def.non_space_wrap;
  style.indented_word_wrap = def.indented_word_wrap;
  style.has_justify_h = def.has_justify_h && !style.justify_h.empty();
  style.has_justify_v = def.has_justify_v && !style.justify_v.empty();
  style.has_outline = def.has_outline;
  style.has_monochrome = def.has_monochrome;
  style.has_non_space_wrap = def.has_non_space_wrap;
  style.has_indented_word_wrap = def.has_indented_word_wrap;
  return style;
}

bool ParseFontsFromXml(const std::string& xml, std::unordered_map<std::string, GlueFontStyle>* out,
                       std::string* error) {
  if (out == nullptr) return false;

  auto parsed = openwow::ui::ParseFontAndIntrinsicXml(xml);
  if (!parsed.ok) {
    if (error != nullptr) {
      *error = std::move(parsed.error);
    }
    return false;
  }

  for (const auto& def : parsed.fonts) {
    GlueFontStyle style = ToGlueFontStyle(def);
    if (!style.name.empty()) {
      out->insert_or_assign(ToLowerAscii(style.name), std::move(style));
    }
  }
  return true;
}

}

std::optional<GlueFontRegistry> GlueFontRegistry::LoadFromVfs(const openwow::vfs::VirtualFileSystem& vfs) {
  GlueFontRegistry reg;
  bool loaded_any = false;

  for (const auto& path : {"/Interface/GlueXML/GlueFonts.xml", "/Interface/GlueXML/GlueFontStyles.xml"}) {
    const auto xml = vfs.ReadTextFile(path);
    if (!xml.has_value() || xml->empty()) {
      continue;
    }
    loaded_any = true;
    std::string error;
    if (!ParseFontsFromXml(*xml, &reg.styles_, &error)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueFontRegistry: failed to parse " + std::string(path) +
                             (error.empty() ? std::string{} : ": " + error));
    }
  }

  if (!loaded_any || reg.styles_.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueFontRegistry: missing GlueFonts.xml/GlueFontStyles.xml; FontString rendering disabled");
    return std::nullopt;
  }

  return reg;
}

bool GlueFontRegistry::HasStyle(const std::string& style_name) const {
  return styles_.find(ToLowerAscii(Trim(style_name))) != styles_.end();
}

std::optional<GlueFontStyle> GlueFontRegistry::Resolve(const std::string& style_name) const {
  const std::string key = ToLowerAscii(Trim(style_name));
  if (key.empty()) return std::nullopt;
  std::unordered_map<std::string, GlueFontStyle> memo;
  std::unordered_map<std::string, bool> stack;
  memo.reserve(styles_.size());
  stack.reserve(styles_.size());
  return ResolveImpl(key, &memo, &stack);
}

std::optional<GlueFontStyle> GlueFontRegistry::ResolveImpl(
    const std::string& style_name,
    std::unordered_map<std::string, GlueFontStyle>* memo,
    std::unordered_map<std::string, bool>* stack) const {
  if (memo == nullptr || stack == nullptr) return std::nullopt;

  const auto memo_it = memo->find(style_name);
  if (memo_it != memo->end()) {
    return memo_it->second;
  }

  const auto it = styles_.find(style_name);
  if (it == styles_.end()) {
    return std::nullopt;
  }
  if ((*stack)[style_name]) {
    return std::nullopt;
  }
  (*stack)[style_name] = true;

  GlueFontStyle resolved = it->second;
  const GlueFontStyle own = resolved;
  resolved = {};

  const std::string parent = ToLowerAscii(Trim(own.inherits));
  if (!parent.empty()) {
    if (const auto parent_res = ResolveImpl(parent, memo, stack); parent_res.has_value()) {
      resolved = *parent_res;
    }
  }
  const std::string font_reference = ToLowerAscii(Trim(own.font_file));
  const bool references_named_font = !font_reference.empty() && styles_.contains(font_reference);
  if (references_named_font) {
    if (const auto referenced = ResolveImpl(font_reference, memo, stack); referenced.has_value()) {
      resolved = *referenced;
    }
  }

  resolved.name = own.name;
  resolved.inherits = own.inherits;
  if (own.has_font_file && own.has_height && !references_named_font) {
    resolved.font_file = own.font_file;
    resolved.has_font_file = true;
    resolved.height_px = own.height_px;
    resolved.has_height = true;
    resolved.outline = own.outline;
    resolved.has_outline = own.has_outline;
    resolved.monochrome = own.monochrome;
    resolved.has_monochrome = own.has_monochrome;
  }
  if (own.has_color) {
    resolved.color_r = own.color_r;
    resolved.color_g = own.color_g;
    resolved.color_b = own.color_b;
    resolved.color_a = own.color_a;
    resolved.has_color = true;
  }
  if (own.has_shadow) {
    resolved.shadow_x_px = own.shadow_x_px;
    resolved.shadow_y_px = own.shadow_y_px;
    resolved.shadow_r = own.shadow_r;
    resolved.shadow_g = own.shadow_g;
    resolved.shadow_b = own.shadow_b;
    resolved.shadow_a = own.shadow_a;
    resolved.has_shadow = true;
  }
  if (own.has_spacing) {
    resolved.spacing_px = own.spacing_px;
    resolved.has_spacing = true;
  }
  if (own.has_justify_h) {
    resolved.justify_h = own.justify_h;
    resolved.has_justify_h = true;
  }
  if (own.has_justify_v) {
    resolved.justify_v = own.justify_v;
    resolved.has_justify_v = true;
  }
  if (own.has_non_space_wrap) {
    resolved.non_space_wrap = own.non_space_wrap;
    resolved.has_non_space_wrap = true;
  }
  if (own.has_indented_word_wrap) {
    resolved.indented_word_wrap = own.indented_word_wrap;
    resolved.has_indented_word_wrap = true;
  }

  (*stack)[style_name] = false;
  memo->insert_or_assign(style_name, resolved);
  return resolved;
}

}

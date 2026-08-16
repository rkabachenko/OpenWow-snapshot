
#include "openwow/ui/framexml_font_registry.h"

#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace openwow::ui {

using openwow::text::EqualsIgnoreCaseAscii;
using openwow::text::ToLowerAscii;
using openwow::text::Trim;

void FontDefinitionRegistry::Register(FontDefinition def) {
  if (!def.outline.empty()) def.has_outline = true;
  if (def.monochrome) def.has_monochrome = true;
  auto name = def.name;
  defs_.insert_or_assign(std::move(name), std::move(def));
}

std::string FontDefinitionRegistry::RegisterFromXml(
    const std::string& xml_text) {
  auto parsed = ParseFontXml(xml_text);
  if (!parsed.has_value()) {
    return {};
  }

  auto name = parsed->name;
  Register(std::move(*parsed));
  return name;
}

bool FontDefinitionRegistry::Unregister(const std::string& name) {
  return defs_.erase(name) > 0;
}

std::optional<FontDefinition> FontDefinitionRegistry::Get(
    const std::string& name) const {
  auto it = defs_.find(name);
  if (it == defs_.end()) {
    return std::nullopt;
  }
  return Resolve(it->second);
}

const FontDefinition* FontDefinitionRegistry::GetRaw(
    const std::string& name) const {
  auto it = defs_.find(name);
  return it != defs_.end() ? &it->second : nullptr;
}

bool FontDefinitionRegistry::Has(const std::string& name) const {
  return defs_.contains(name);
}

std::vector<std::string> FontDefinitionRegistry::GetNames() const {
  std::vector<std::string> names;
  names.reserve(defs_.size());
  for (const auto& [name, _] : defs_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

FontDefinition FontDefinitionRegistry::Resolve(
    const FontDefinition& def) const {
  std::unordered_set<std::string> active;
  std::function<FontDefinition(const FontDefinition&)> resolve =
      [&](const FontDefinition& current) -> FontDefinition {
    FontDefinition result;
    const bool inserted = current.name.empty() || active.insert(current.name).second;
    if (!inserted) {
      result.name = current.name;
      result.inherits = current.inherits;
      result.is_virtual = current.is_virtual;
      return result;
    }

    if (!current.inherits.empty()) {
      if (const auto parent = defs_.find(current.inherits); parent != defs_.end()) {
        result = resolve(parent->second);
      }
    }

    result.name = current.name;
    result.inherits = current.inherits;
    result.is_virtual = current.is_virtual;

    if (!current.font_file.empty()) {
      if (const auto font_ref = defs_.find(current.font_file); font_ref != defs_.end()) {
        FontDefinition referenced = resolve(font_ref->second);
        referenced.name = result.name;
        referenced.inherits = result.inherits;
        referenced.is_virtual = result.is_virtual;
        result = std::move(referenced);
      } else if (current.has_height) {
        result.font_file = current.font_file;
        result.height = current.height;
        result.has_height = true;
        result.outline = current.outline;
        result.has_outline = current.has_outline;
        result.monochrome = current.monochrome;
        result.has_monochrome = current.has_monochrome;
      }
    }
    if (current.has_color) {
      result.color = current.color;
      result.has_color = true;
    }
    if (current.shadow.has_value()) result.shadow = current.shadow;
    if (current.has_spacing) {
      result.spacing = current.spacing;
      result.has_spacing = true;
    }
    if (current.has_justify_h) {
      result.justify_h = current.justify_h;
      result.has_justify_h = true;
    }
    if (current.has_justify_v) {
      result.justify_v = current.justify_v;
      result.has_justify_v = true;
    }
    if (current.has_non_space_wrap) {
      result.non_space_wrap = current.non_space_wrap;
      result.has_non_space_wrap = true;
    }
    if (current.has_indented_word_wrap) {
      result.indented_word_wrap = current.indented_word_wrap;
      result.has_indented_word_wrap = true;
    }

    if (!current.name.empty()) active.erase(current.name);
    return result;
  };
  return resolve(def);
}

void FontDefinitionRegistry::Clear() {
  defs_.clear();
}

void IntrinsicTypeRegistry::Register(IntrinsicTypeDef def) {
  auto name = def.type_name;
  defs_.insert_or_assign(std::move(name), std::move(def));
}

bool IntrinsicTypeRegistry::IsIntrinsic(const std::string& type_name) const {
  return defs_.contains(type_name);
}

const IntrinsicTypeDef* IntrinsicTypeRegistry::Get(
    const std::string& type_name) const {
  auto it = defs_.find(type_name);
  return it != defs_.end() ? &it->second : nullptr;
}

std::vector<std::string> IntrinsicTypeRegistry::GetNames() const {
  std::vector<std::string> names;
  names.reserve(defs_.size());
  for (const auto& [name, _] : defs_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

void IntrinsicTypeRegistry::Clear() {
  defs_.clear();
}

namespace {

bool ParseRootDocument(const std::string& xml_text, xml::XMLNode* out_root,
                       std::string* error) {
  if (xml::FrameXMLParser::ParseDocument(xml_text, out_root, error)) {
    return true;
  }

  const std::string wrapped = "<Ui>" + xml_text + "</Ui>";
  return xml::FrameXMLParser::ParseDocument(wrapped, out_root, error);
}

const xml::XMLNode* FindSingleFontNode(const xml::XMLNode& root) {
  if (EqualsIgnoreCaseAscii(root.tag, "Font")) {
    return &root;
  }
  if (EqualsIgnoreCaseAscii(root.tag, "Ui")) {
    for (const auto& child : root.children) {
      if (EqualsIgnoreCaseAscii(child.tag, "Font")) {
        return &child;
      }
    }
  }
  return nullptr;
}

std::optional<float> ParseNumberAttr(const xml::XMLNode& node,
                                     const char* attr_name) {
  if (!node.HasAttr(attr_name)) {
    return std::nullopt;
  }
  return node.GetAttrFloat(attr_name);
}

float ClampColor(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

FontColor ParseColorNode(const xml::XMLNode& node) {
  FontColor color;
  if (auto v = ParseNumberAttr(node, "r"); v.has_value()) color.r = ClampColor(*v);
  if (auto v = ParseNumberAttr(node, "g"); v.has_value()) color.g = ClampColor(*v);
  if (auto v = ParseNumberAttr(node, "b"); v.has_value()) color.b = ClampColor(*v);
  if (auto v = ParseNumberAttr(node, "a"); v.has_value()) color.a = ClampColor(*v);
  return color;
}

std::optional<FontShadow> ParseShadowNode(const xml::XMLNode& node) {
  FontShadow shadow;
  bool has_shadow = false;

  if (const auto* offset = node.FindChild("Offset"); offset != nullptr) {
    float offset_x = shadow.offset.x;
    float offset_y = shadow.offset.y;
    if (xml::RelDimension_ref(offset, &offset_x, &offset_y, nullptr) != 0) {
      shadow.offset.x = offset_x;
      shadow.offset.y = offset_y;
      has_shadow = true;
    }
  }

  if (const auto* color = node.FindChild("Color"); color != nullptr) {
    shadow.color = ParseColorNode(*color);
    has_shadow = true;
  }

  return has_shadow ? std::optional<FontShadow>(shadow) : std::nullopt;
}

bool IsFrameTypeTag(std::string_view tag) {
  static constexpr std::array<std::string_view, 20> kFrameTags = {
      "Frame",       "Button",      "CheckButton",   "Slider",
      "ScrollFrame", "EditBox",     "MessageFrame",  "ColorSelect",
      "Model",       "PlayerModel", "DressUpModel",  "ModelFFX",
      "Minimap",     "GameTooltip", "StatusBar",     "Cooldown",
      "ScrollingMessageFrame", "SimpleHTML", "WorldFrame", "MovieFrame",
  };

  for (const auto candidate : kFrameTags) {
    if (EqualsIgnoreCaseAscii(tag, candidate)) {
      return true;
    }
  }
  return false;
}

FontDefinition ParseFontNode(const xml::XMLNode& node) {
  FontDefinition def;
  def.name = Trim(node.GetAttr("name"));
  def.font_file = Trim(node.GetAttr("font"));
  def.inherits = Trim(node.GetAttr("inherits"));
  def.is_virtual = node.GetAttrBool("virtual");
  def.outline = Trim(node.GetAttr("outline"));
  def.has_outline = node.HasAttr("outline");
  def.monochrome = node.GetAttrBool("monochrome");
  def.has_monochrome = node.HasAttr("monochrome");

  if (node.HasAttr("spacing")) {
    def.spacing =
        openwow::ui::PixelUiHorizontalCoordinateToStored(node.GetAttrFloat("spacing"));
    def.has_spacing = true;
  }
  if (node.HasAttr("justifyH")) {
    def.justify_h = Trim(node.GetAttr("justifyH"));
    def.has_justify_h = !def.justify_h.empty();
  }
  if (node.HasAttr("justifyV")) {
    def.justify_v = Trim(node.GetAttr("justifyV"));
    def.has_justify_v = !def.justify_v.empty();
  }
  if (node.HasAttr("nonspacewrap")) {
    def.non_space_wrap = node.GetAttrBool("nonspacewrap");
    def.has_non_space_wrap = true;
  }
  if (node.HasAttr("indented")) {
    def.indented_word_wrap = node.GetAttrBool("indented");
    def.has_indented_word_wrap = true;
  }

  if (const auto* height = node.FindChild("FontHeight"); height != nullptr) {
    float resolved_height = 0.0f;
    if (xml::RelValue_ref(height, &resolved_height, nullptr) != 0) {
      def.height = resolved_height;
      def.has_height = true;
    }
  }

  if (const auto* color = node.FindChild("Color"); color != nullptr) {
    def.color = ParseColorNode(*color);
    def.has_color = true;
  }

  if (const auto* shadow = node.FindChild("Shadow"); shadow != nullptr) {
    def.shadow = ParseShadowNode(*shadow);
  }

  return def;
}

}

std::optional<FontDefinition> ParseFontXml(const std::string& xml_text) {
  xml::XMLNode root;
  std::string error;
  if (!ParseRootDocument(xml_text, &root, &error)) {
    return std::nullopt;
  }

  const auto* font_node = FindSingleFontNode(root);
  if (font_node == nullptr) {
    return std::nullopt;
  }

  FontDefinition def = ParseFontNode(*font_node);
  if (def.name.empty()) {
    return std::nullopt;
  }

  return def;
}

FontXmlParseResult ParseFontAndIntrinsicXml(const std::string& xml_text) {
  xml::XMLNode root;
  std::string error;
  if (!ParseRootDocument(xml_text, &root, &error)) {
    FontXmlParseResult result;
    result.error = std::move(error);
    return result;
  }

  return ParseFontAndIntrinsicXml(root);
}

FontXmlParseResult ParseFontAndIntrinsicXml(const xml::XMLNode& root) {
  FontXmlParseResult result;

  const xml::XMLNode* ui_root = EqualsIgnoreCaseAscii(root.tag, "Ui") ? &root : nullptr;
  if (ui_root == nullptr) {
    result.error = "missing <Ui> root";
    return result;
  }

  for (const auto& child : ui_root->children) {
    if (EqualsIgnoreCaseAscii(child.tag, "Font")) {
      FontDefinition def = ParseFontNode(child);
      if (!def.name.empty()) {
        result.fonts.push_back(std::move(def));
      }
      continue;
    }

    if (!IsFrameTypeTag(child.tag)) {
      continue;
    }

    if (!child.GetAttrBool("intrinsic")) {
      continue;
    }

    IntrinsicTypeDef def;
    def.type_name = Trim(child.GetAttr("name"));
    def.inherits = Trim(child.GetAttr("inherits"));
    def.is_virtual = child.GetAttrBool("virtual", true);
    if (!def.type_name.empty()) {
      result.intrinsics.push_back(def);
      result.skipped_intrinsic_frames.push_back(def.type_name);
    }
  }

  result.ok = true;
  return result;
}

}

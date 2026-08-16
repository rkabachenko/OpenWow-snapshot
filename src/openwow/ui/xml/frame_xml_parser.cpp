
#include "openwow/ui/xml/frame_xml_parser.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/xml/xml_tree.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <charconv>

namespace openwow::ui::xml {
namespace {

std::unordered_map<std::string, XMLFrameDef>& LegacyTemplateProjections() {
  static std::unordered_map<std::string, XMLFrameDef> projections;
  return projections;
}

}

using openwow::text::ToLowerAscii;
using openwow::text::Trim;

std::string XMLNode::GetAttr(const std::string& name,
                             const std::string& def) const {
  auto it = attributes.find(name);
  if (it != attributes.end()) return it->second;

  const auto lower = ToLowerAscii(name);
  for (const auto& [k, v] : attributes) {
    if (ToLowerAscii(k) == lower) return v;
  }
  return def;
}

bool XMLNode::HasAttr(const std::string& name) const {
  if (attributes.count(name)) return true;
  const auto lower = ToLowerAscii(name);
  for (const auto& [k, v] : attributes) {
    if (ToLowerAscii(k) == lower) return true;
  }
  return false;
}

int XMLNode::GetAttrInt(const std::string& name, int def) const {
  const auto s = Trim(GetAttr(name));
  if (s.empty()) return def;
  int val = def;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  return (ec == std::errc()) ? val : def;
}

float XMLNode::GetAttrFloat(const std::string& name, float def) const {
  const auto s = Trim(GetAttr(name));
  if (s.empty()) return def;
  return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(s));
}

bool XMLNode::GetAttrBool(const std::string& name, bool def) const {
  const auto s = ToLowerAscii(Trim(GetAttr(name)));
  if (s.empty()) return def;
  if (s == "true" || s == "1" || s == "yes") return true;
  if (s == "false" || s == "0" || s == "no") return false;
  return def;
}

const XMLNode* XMLNode::FindChild(const std::string& tagName) const {
  const auto lower = ToLowerAscii(tagName);
  for (const auto& child : children) {
    if (ToLowerAscii(child.tag) == lower) return &child;
  }
  return nullptr;
}

std::vector<const XMLNode*> XMLNode::FindChildren(
    const std::string& tagName) const {
  std::vector<const XMLNode*> result;
  const auto lower = ToLowerAscii(tagName);
  for (const auto& child : children) {
    if (ToLowerAscii(child.tag) == lower) result.push_back(&child);
  }
  return result;
}

namespace {

XMLNode ConvertXmlTreeNode(const CXMLNode& node) {
  XMLNode converted;
  converted.tag = node.tag;
  if (node.text != nullptr && node.text_size > 0) {
    converted.text.assign(node.text, node.text + node.text_size);
  }
  for (const auto& attr : node.attributes.entries) {
    converted.attributes[attr.name] = attr.value;
  }
  for (const CXMLNode* child = node.first_child; child != nullptr; child = child->right_sibling) {
    converted.children.push_back(ConvertXmlTreeNode(*child));
  }
  return converted;
}

bool IsFrameType(const std::string& tag) {
  static const std::vector<std::string> types = {
      "frame",       "button",      "checkbutton",   "slider",
      "scrollframe", "editbox",     "messageframe",  "colorselect",
      "model",       "playermodel", "dressupmodel",  "modelffx",
      "minimap",     "gametooltip", "statusbar",     "cooldown",
      "scrollingmessageframe", "simplehtml",  "worldframe",
      "texture",     "fontstring",  "movieframe",    "tabardmodel",
  };
  const auto lower = ToLowerAscii(tag);
  for (const auto& t : types) {
    if (lower == t) return true;
  }
  return false;
}

bool IsLayerItem(const std::string& tag) {
  const auto lower = ToLowerAscii(tag);
  return lower == "texture" || lower == "fontstring" || lower == "line";
}

float ClampFontColorComponent(const float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

void ParseFontColorNode(const XMLNode& node, XMLFrameDef::FontColorDef* out_color) {
  out_color->r = ClampFontColorComponent(node.GetAttrFloat("r", 0.0f));
  out_color->g = ClampFontColorComponent(node.GetAttrFloat("g", 0.0f));
  out_color->b = ClampFontColorComponent(node.GetAttrFloat("b", 0.0f));
  out_color->a = ClampFontColorComponent(node.GetAttrFloat("a", 1.0f));
}

void ParseFontStringVisualChildren(const XMLNode& node, XMLFrameDef* def) {
  const auto* font_height_node = node.FindChild("FontHeight");
  if (font_height_node != nullptr) {
    float resolved_height = 0.0f;
    if (RelValue_ref(font_height_node, &resolved_height, nullptr) != 0) {
      def->font_height = resolved_height;
    }
  }

  for (const auto& child : node.children) {
    const auto lower_tag = ToLowerAscii(child.tag);
    if (lower_tag == "color") {
      ParseFontColorNode(child, &def->text_color);
      def->has_text_color = true;
      continue;
    }

    if (lower_tag != "shadow") {
      continue;
    }

    def->has_shadow = true;
    XMLFrameDef::FontShadowDef parsed_shadow;
    if (const auto* color = child.FindChild("Color"); color != nullptr) {
      XMLFrameDef::FontColorDef parsed_color;
      ParseFontColorNode(*color, &parsed_color);
      parsed_shadow.r = parsed_color.r;
      parsed_shadow.g = parsed_color.g;
      parsed_shadow.b = parsed_color.b;
      parsed_shadow.a = parsed_color.a;
    }

    if (const auto* offset = child.FindChild("Offset"); offset != nullptr) {
      float offset_x = parsed_shadow.x;
      float offset_y = parsed_shadow.y;
      if (RelDimension_ref(offset, &offset_x, &offset_y, nullptr) != 0) {
        parsed_shadow.x = offset_x;
        parsed_shadow.y = offset_y;
      }
    }

    def->shadow = parsed_shadow;
  }
}

}

bool FrameXMLParser::ParseDocument(const std::string& xml_text, XMLNode* out_root,
                                   std::string* error) {
  if (out_root == nullptr) {
    if (error != nullptr) {
      *error = "output root is null";
    }
    return false;
  }

  CXMLTree* tree = XMLTree_Parse(xml_text.data(), xml_text.size());
  if (tree == nullptr) {
    if (error != nullptr) {
      *error = "Failed to parse XML document";
    }
    return false;
  }

  if (tree->root == nullptr) {
    XMLTree_Free(tree);
    if (error != nullptr) {
      *error = "No XML content found";
    }
    return false;
  }

  *out_root = ConvertXmlTreeNode(*tree->root);
  XMLTree_Free(tree);
  if (error != nullptr) {
    error->clear();
  }
  return true;
}

void FrameXMLParser::ParseAnchors(const XMLNode& node, XMLFrameDef& def) {
  const auto* anchors_node = node.FindChild("Anchors");
  if (!anchors_node) return;

  for (const auto* anchor_node : anchors_node->FindChildren("Anchor")) {
    XMLFrameDef::Anchor anchor;
    anchor.point = anchor_node->GetAttr("point");
    anchor.relative_to = anchor_node->GetAttr("relativeTo");
    anchor.relative_point = anchor_node->GetAttr("relativePoint");
    anchor.x_offset = anchor_node->GetAttrFloat("x");
    anchor.y_offset = anchor_node->GetAttrFloat("y");

    const auto* offset = anchor_node->FindChild("Offset");
    if (offset) {
      anchor.x_offset = offset->GetAttrFloat("x");
      anchor.y_offset = offset->GetAttrFloat("y");
    }

    if (offset) {
      const auto* abs_dim = offset->FindChild("AbsDimension");
      if (abs_dim) {
        anchor.x_offset = abs_dim->GetAttrFloat("x");
        anchor.y_offset = abs_dim->GetAttrFloat("y");
      }
    }

    def.anchors.push_back(std::move(anchor));
  }
}

void FrameXMLParser::ParseSize(const XMLNode& node, XMLFrameDef& def) {
  const auto* size_node = node.FindChild("Size");
  if (!size_node) return;

  if (size_node->HasAttr("x") || size_node->HasAttr("y")) {
    def.size.x = size_node->GetAttrFloat("x");
    def.size.y = size_node->GetAttrFloat("y");
    def.size.set = true;
    return;
  }

  const auto* abs_dim = size_node->FindChild("AbsDimension");
  if (abs_dim) {
    def.size.x = abs_dim->GetAttrFloat("x");
    def.size.y = abs_dim->GetAttrFloat("y");
    def.size.set = true;
  }
}

void FrameXMLParser::ParseLayers(const XMLNode& node, XMLFrameDef& def) {
  const auto* layers_node = node.FindChild("Layers");
  if (!layers_node) return;

  for (const auto* layer_node : layers_node->FindChildren("Layer")) {
    XMLFrameDef::Layer layer;
    layer.level = layer_node->GetAttr("level");

    for (const auto& child : layer_node->children) {
      if (IsLayerItem(child.tag)) {
        layer.items.push_back(ParseFrameNode(child));
      }
    }

    def.layers.push_back(std::move(layer));
  }
}

void FrameXMLParser::ParseScripts(const XMLNode& node, XMLFrameDef& def) {
  const auto* scripts_node = node.FindChild("Scripts");
  if (!scripts_node) return;

  for (const auto& child : scripts_node->children) {
    XMLFrameDef::Script script;
    script.handler = child.tag;
    script.body = child.text;
    script.function = child.GetAttr("function");
    def.scripts.push_back(std::move(script));
  }
}

void FrameXMLParser::ParseBackdrop(const XMLNode& node, XMLFrameDef& def) {
  const auto* bd_node = node.FindChild("Backdrop");
  if (!bd_node) return;

  def.backdrop.set = true;
  def.backdrop.bg_file = bd_node->GetAttr("bgFile");
  def.backdrop.edge_file = bd_node->GetAttr("edgeFile");
  def.backdrop.tile = bd_node->GetAttrBool("tile");
  def.backdrop.tile_size = bd_node->GetAttrInt("tileSize");
  def.backdrop.edge_size = bd_node->GetAttrInt("edgeSize");

  const auto* insets = bd_node->FindChild("BackgroundInsets");
  if (insets) {
    const auto* abs = insets->FindChild("AbsInset");
    if (abs) {
      def.backdrop.insets.left   = abs->GetAttrFloat("left");
      def.backdrop.insets.right  = abs->GetAttrFloat("right");
      def.backdrop.insets.top    = abs->GetAttrFloat("top");
      def.backdrop.insets.bottom = abs->GetAttrFloat("bottom");
    } else {
      def.backdrop.insets.left   = insets->GetAttrFloat("left");
      def.backdrop.insets.right  = insets->GetAttrFloat("right");
      def.backdrop.insets.top    = insets->GetAttrFloat("top");
      def.backdrop.insets.bottom = insets->GetAttrFloat("bottom");
    }
  }

  const auto* es = bd_node->FindChild("EdgeSize");
  if (es) {
    const auto* abs = es->FindChild("AbsValue");
    if (abs) def.backdrop.edge_size = abs->GetAttrInt("val");
    else     def.backdrop.edge_size = es->GetAttrInt("val", def.backdrop.edge_size);
  }
  const auto* ts = bd_node->FindChild("TileSize");
  if (ts) {
    const auto* abs = ts->FindChild("AbsValue");
    if (abs) def.backdrop.tile_size = abs->GetAttrInt("val");
    else     def.backdrop.tile_size = ts->GetAttrInt("val", def.backdrop.tile_size);
  }
}

void FrameXMLParser::ParseAttributes(const XMLNode& node, XMLFrameDef& def) {
  const auto* attrs_node = node.FindChild("Attributes");
  if (!attrs_node) return;

  for (const auto* attr_node : attrs_node->FindChildren("Attribute")) {
    XMLFrameDef::KeyValue kv;
    kv.key   = attr_node->GetAttr("name");
    kv.value = attr_node->GetAttr("value");
    kv.type  = attr_node->GetAttr("type", "string");
    if (!kv.key.empty()) {
      def.kv_attributes.push_back(std::move(kv));
    }
  }
}

XMLFrameDef FrameXMLParser::ParseFrameNode(const XMLNode& node) {
  XMLFrameDef def;
  def.raw_node = node;
  def.type = node.tag;
  def.name = node.GetAttr("name");
  def.parent = node.GetAttr("parent");
  def.inherits = node.GetAttr("inherits");
  def.is_virtual = node.GetAttrBool("virtual");
  def.hidden = node.GetAttrBool("hidden");
  def.toplevel = node.GetAttrBool("toplevel");
  def.alpha = node.GetAttrFloat("alpha", 1.0f);
  def.id = static_cast<uint32_t>(node.GetAttrInt("id"));
  def.font = node.GetAttr("font");
  def.text = node.GetAttr("text");
  def.file = node.GetAttr("file");
  def.attributes = node.attributes;

  ParseSize(node, def);
  ParseAnchors(node, def);
  ParseLayers(node, def);
  ParseScripts(node, def);
  ParseBackdrop(node, def);
  ParseAttributes(node, def);
  if (ToLowerAscii(def.type) == "fontstring") {
    ParseFontStringVisualChildren(node, &def);
  }

  const auto* frames_node = node.FindChild("Frames");
  if (frames_node) {
    for (const auto& child : frames_node->children) {
      if (IsFrameType(child.tag)) {
        def.children.push_back(ParseFrameNode(child));
      }
    }
  }

  return def;
}

FrameXMLParser::ParseResult FrameXMLParser::Parse(
    const std::string& xml_text, const std::string& ) {
  ParseResult result;

  std::string error;
  XMLNode root;
  if (!ParseDocument(xml_text, &root, &error)) {
    result.error = error;
    return result;
  }

  const auto root_lower = ToLowerAscii(root.tag);
  if (root_lower != "ui") {
    result.error = "Expected <Ui> root element, got <" + root.tag + ">";
    return result;
  }

  for (const auto& child : root.children) {
    const auto child_lower = ToLowerAscii(child.tag);

    if (child_lower == "script") {
      const auto file = child.GetAttr("file");
      if (!file.empty()) {
        result.scripts.push_back(file);
      }
      continue;
    }

    if (child_lower == "include") {
      const auto file = child.GetAttr("file");
      if (!file.empty()) {
        result.includes.push_back(file);
      }
      continue;
    }

    if (IsFrameType(child.tag)) {
      auto frame_def = ParseFrameNode(child);

      if (frame_def.is_virtual) {
        result.templates.push_back(std::move(frame_def));
      } else {
        result.frames.push_back(std::move(frame_def));
      }
    }
  }

  const auto canonical = openwow::ui::framexml::ParseFrameXml(root);
  if (!canonical.ok) {
    result.error = canonical.error;
    return result;
  }
  for (const auto& frame : canonical.frames) {
    if (frame.virtual_template && !frame.name.empty()) {
      openwow::ui::framexml::RegisterVirtualTemplate(frame);
    }
  }

  result.success = true;
  return result;
}

const XMLFrameDef* FrameXMLParser::GetTemplate(const std::string& name) {
  const auto* canonical = openwow::ui::framexml::GetVirtualTemplate(name);
  if (canonical == nullptr || canonical->authored_xml == nullptr) {
    return nullptr;
  }

  auto& projections = LegacyTemplateProjections();
  auto [it, _] = projections.insert_or_assign(
      name, ParseFrameNode(*canonical->authored_xml));
  return &it->second;
}

void FrameXMLParser::ClearTemplates() {
  openwow::ui::framexml::ClearVirtualTemplates();
  LegacyTemplateProjections().clear();
}

}

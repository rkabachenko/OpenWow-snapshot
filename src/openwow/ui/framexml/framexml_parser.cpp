#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/framexml/framexml_parser_detail.h"
#include "openwow/ui/framexml/framexml_value_utils.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/animation/alpha_anim.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::ui::framexml {

namespace {

using openwow::text::EqualsIgnoreCaseAscii;
using openwow::text::ToLowerAscii;
using openwow::text::ToUpperAscii;
using openwow::text::Trim;
using XmlNode = openwow::ui::xml::XMLNode;

struct LayerContext {
  std::string level;
  int sublevel{0};
};

struct ParentContext {
  std::size_t index{0};

  std::string attribute_parent_token_name;
  std::string token_parent_name;
  std::string token_parent_kind;
  std::optional<std::size_t> top_level_group_index;
};

struct ParserContext {
  ParseResult result{.ok = false, .error = "", .frames = {}, .diagnostics = {}};
  std::unordered_map<std::string, std::size_t> index_by_name;
  std::vector<ParentContext> stack;
  std::vector<LayerContext> layer_stack;
  int unnamed_texture_counter{0};
  int unnamed_fontstring_counter{0};
  int unnamed_widget_counter{0};
  int scroll_child_depth{0};
};

std::unordered_map<std::string, UiFrame>& VirtualTemplateRegistry() {
  static std::unordered_map<std::string, UiFrame> registry;
  return registry;
}

std::string Attr(const XmlNode& node, const std::string& name) {
  return node.GetAttr(name);
}

std::optional<float> FloatAttr(const XmlNode& node, const std::string& name) {
  const std::string raw = Trim(Attr(node, name));
  if (raw.empty()) {
    return std::nullopt;
  }
  return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(raw));
}

std::optional<int> IntAttr(const XmlNode& node, const std::string& name) {
  return ParseIntegerAttributeValue(Attr(node, name));
}

std::optional<float> FirstFloatAttr(const XmlNode& node,
                                    std::initializer_list<std::string_view> names) {
  for (const auto name : names) {
    if (auto value = FloatAttr(node, std::string(name)); value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<int> FirstIntAttr(const XmlNode& node,
                                std::initializer_list<std::string_view> names) {
  for (const auto name : names) {
    if (auto value = IntAttr(node, std::string(name)); value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<bool> OptionalBoolAttr(const XmlNode& node, const std::string& name) {
  const std::string text = Attr(node, name);
  if (text.empty()) {
    return std::nullopt;
  }

  switch (text[0]) {
    case '0':
    case 'F':
    case 'N':
    case 'f':
    case 'n':
      return false;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'T':
    case 'Y':
    case 't':
    case 'y':
      return true;
    default:
      break;
  }

  if (EqualsIgnoreCaseAscii(text, "off") || EqualsIgnoreCaseAscii(text, "disabled")) {
    return false;
  }
  if (EqualsIgnoreCaseAscii(text, "on") || EqualsIgnoreCaseAscii(text, "enabled")) {
    return true;
  }
  return false;
}

bool BoolAttr(const XmlNode& node, const std::string& name) {
  return OptionalBoolAttr(node, name).value_or(false);
}

bool IsModelLikeKind(std::string_view kind) {
  return EqualsIgnoreCaseAscii(kind, "Model") || EqualsIgnoreCaseAscii(kind, "ModelFFX") ||
         EqualsIgnoreCaseAscii(kind, "PlayerModel") ||
         EqualsIgnoreCaseAscii(kind, "DressUpModel") ||
         EqualsIgnoreCaseAscii(kind, "TabardModel");
}

const XmlNode* FirstDirectChild(const XmlNode& node, std::string_view name) {
  const std::string want = ToLowerAscii(std::string(name));
  for (const auto& child : node.children) {
    if (ToLowerAscii(child.tag) == want) {
      return &child;
    }
  }
  return nullptr;
}

std::vector<const XmlNode*> DirectChildren(const XmlNode& node, std::string_view name) {
  const std::string want = ToLowerAscii(std::string(name));
  std::vector<const XmlNode*> children;
  for (const auto& child : node.children) {
    if (ToLowerAscii(child.tag) == want) {
      children.push_back(&child);
    }
  }
  return children;
}

std::string FirstDirectChildText(const XmlNode& node, std::string_view name) {
  const auto* child = FirstDirectChild(node, name);
  return child != nullptr ? Trim(child->text) : std::string{};
}

std::string CurrentTokenParentName(const ParserContext& ctx) {
  return ctx.stack.empty() ? std::string{} : ctx.stack.back().token_parent_name;
}

std::string CurrentStructuralParentName(const ParserContext& ctx) {
  if (ctx.stack.empty() ||
      ctx.stack.back().index >= ctx.result.frames.size()) {
    return {};
  }
  return ctx.result.frames[ctx.stack.back().index].name;
}

std::string CurrentTokenParentKind(const ParserContext& ctx) {
  return ctx.stack.empty() ? std::string{} : ctx.stack.back().token_parent_kind;
}

bool IsGeneratedAnonymousName(std::string_view name) {
  return name.find(".__Anon") != std::string_view::npos;
}

std::string NormalizeScriptFrameStrata(const std::string& raw_frame_strata,
                                       std::string_view diagnostic_frame_name,
                                       std::vector<std::string>* diagnostics) {
  if (raw_frame_strata.empty()) {
    return {};
  }

  int strata_value = 0;
  if (openwow::ui::StringToScriptFrameStrata(raw_frame_strata.c_str(), &strata_value) == 0) {
    if (diagnostics != nullptr) {
      const std::string frame_name =
          diagnostic_frame_name.empty() ? "<unnamed>" : std::string(diagnostic_frame_name);
      diagnostics->push_back("Frame " + frame_name +
                             ": Unknown frame strata: " + raw_frame_strata);
    }
    return {};
  }

  return openwow::ui::ScriptFrameStrataToString(strata_value);
}

std::vector<UiAnchor> ParseAnchors(const XmlNode& node,
                                   std::vector<std::string>* diagnostics) {
  std::vector<UiAnchor> anchors;
  const auto *anchors_node = FirstDirectChild(node, "Anchors");
  if (anchors_node == nullptr) {
    return anchors;
  }

  for (const auto *anchor_node : DirectChildren(*anchors_node, "Anchor")) {
    UiAnchor anchor;
    const std::string raw_point = Attr(*anchor_node, "point");
    int point_value = 0;
    if (openwow::ui::StringToFramePoint(raw_point.c_str(), &point_value) == 0) {
      if (diagnostics != nullptr) {
        diagnostics->push_back("Invalid anchor point in frame: " + raw_point);
      }
      continue;
    }
    anchor.point = openwow::ui::FramePointToString(point_value);
    anchor.relative_to = Attr(*anchor_node, "relativeTo");
    anchor.relative_to_explicit = !Trim(anchor.relative_to).empty();
    const std::string raw_relative_point = Attr(*anchor_node, "relativePoint");
    if (raw_relative_point.empty()) {
      anchor.relative_point = anchor.point;
    } else {
      int relative_point_value = 0;
      if (openwow::ui::StringToFramePoint(raw_relative_point.c_str(),
                                          &relative_point_value) == 0) {
        if (diagnostics != nullptr) {
          diagnostics->push_back("Invalid anchor point in frame: " +
                                 raw_relative_point);
        }
        continue;
      }
      anchor.relative_point =
          openwow::ui::FramePointToString(relative_point_value);
    }
    anchor.x = FloatAttr(*anchor_node, "x").value_or(0.0F);
    anchor.y = FloatAttr(*anchor_node, "y").value_or(0.0F);

    if (const auto *offset = FirstDirectChild(*anchor_node, "Offset"); offset != nullptr) {
      if (auto x = FloatAttr(*offset, "x"); x.has_value()) {
        anchor.x = *x;
      }
      if (auto y = FloatAttr(*offset, "y"); y.has_value()) {
        anchor.y = *y;
      }
      if (const auto *abs = FirstDirectChild(*offset, "AbsDimension"); abs != nullptr) {
        if (auto x = FloatAttr(*abs, "x"); x.has_value()) {
          anchor.x = *x;
        }
        if (auto y = FloatAttr(*abs, "y"); y.has_value()) {
          anchor.y = *y;
        }
      }
    }

    anchors.push_back(std::move(anchor));
  }

  return anchors;
}

void FillDimensions(UiFrame* frame, const XmlNode& node) {
  if (frame == nullptr || (frame->width.has_value() && frame->height.has_value())) {
    return;
  }

  const auto* size = FirstDirectChild(node, "Size");
  if (size == nullptr) {
    return;
  }

  if (!frame->width.has_value()) {
    frame->width = FloatAttr(*size, "x");
  }
  if (!frame->height.has_value()) {
    frame->height = FloatAttr(*size, "y");
  }
  if (frame->width.has_value() && frame->height.has_value()) {
    return;
  }

  if (const auto* abs = FirstDirectChild(*size, "AbsDimension"); abs != nullptr) {
    if (!frame->width.has_value()) {
      frame->width = FloatAttr(*abs, "x");
    }
    if (!frame->height.has_value()) {
      frame->height = FloatAttr(*abs, "y");
    }
    return;
  }

  if (const auto* rel = FirstDirectChild(*size, "RelDimension"); rel != nullptr) {
    if (!frame->rel_width.has_value()) {
      frame->rel_width = FloatAttr(*rel, "x");
    }
    if (!frame->rel_height.has_value()) {
      frame->rel_height = FloatAttr(*rel, "y");
    }
  }
}

bool ValidateTexCoordValue(float value) {
  constexpr float kMinTexCoord = -10000.0F;
  constexpr float kMaxTexCoord = 10000.0F;
  return value >= kMinTexCoord && value <= kMaxTexCoord;
}

void ReportInvalidTexCoord(const UiFrame& frame,
                           std::string_view kind,
                           std::vector<std::string>* diagnostics) {
  if (diagnostics == nullptr) {
    return;
  }
  const std::string name = frame.name.empty() ? "<unnamed>" : frame.name;
  diagnostics->push_back("Texture " + name + ": Invalid " + std::string(kind) +
                         " value (out of range)");
}

std::optional<float> CheckedTexCoordAttr(const XmlNode& node,
                                         const std::string& name,
                                         const UiFrame& frame,
                                         std::string_view diagnostic_kind,
                                         std::vector<std::string>* diagnostics,
                                         bool* ok) {
  const auto parsed = FloatAttr(node, name);
  if (!parsed.has_value()) {
    return std::nullopt;
  }
  if (!ValidateTexCoordValue(*parsed)) {
    if (ok != nullptr) {
      *ok = false;
    }
    ReportInvalidTexCoord(frame, diagnostic_kind, diagnostics);
    return std::nullopt;
  }
  return parsed;
}

void FillTexCoords(UiFrame* frame, const XmlNode& node, std::vector<std::string>* diagnostics) {
  if (frame == nullptr) {
    return;
  }
  const auto* tex_coords = FirstDirectChild(node, "TexCoords");
  if (tex_coords == nullptr) {
    return;
  }

  UiTextureCoordQuad quad;
  bool ok = true;

  if (const auto* rect = FirstDirectChild(*tex_coords, "Rect"); rect != nullptr) {
    bool rect_ok = true;
    UiTextureCoordQuad parsed_rect;
    parsed_rect.upper_left.u =
        CheckedTexCoordAttr(*rect, "ULx", *frame, "rect", diagnostics, &rect_ok).value_or(0.0F);
    parsed_rect.upper_left.v =
        CheckedTexCoordAttr(*rect, "ULy", *frame, "rect", diagnostics, &rect_ok).value_or(0.0F);
    parsed_rect.lower_left.u =
        CheckedTexCoordAttr(*rect, "LLx", *frame, "rect", diagnostics, &rect_ok).value_or(0.0F);
    parsed_rect.lower_left.v =
        CheckedTexCoordAttr(*rect, "LLy", *frame, "rect", diagnostics, &rect_ok).value_or(1.0F);
    parsed_rect.upper_right.u =
        CheckedTexCoordAttr(*rect, "URx", *frame, "rect", diagnostics, &rect_ok).value_or(1.0F);
    parsed_rect.upper_right.v =
        CheckedTexCoordAttr(*rect, "URy", *frame, "rect", diagnostics, &rect_ok).value_or(0.0F);
    parsed_rect.lower_right.u =
        CheckedTexCoordAttr(*rect, "LRx", *frame, "rect", diagnostics, &rect_ok).value_or(1.0F);
    parsed_rect.lower_right.v =
        CheckedTexCoordAttr(*rect, "LRy", *frame, "rect", diagnostics, &rect_ok).value_or(1.0F);
    if (rect_ok) {
      quad = parsed_rect;
    } else {
      ok = false;
    }
  }

  const auto report_tiling_conflict = [&](std::string_view axis) {
    if (diagnostics == nullptr) {
      return;
    }
    const std::string name = frame->name.empty() ? "<unnamed>" : frame->name;
    diagnostics->push_back("Texture " + name +
                           ": Invalid TexCoords value (" +
                           std::string(axis) + "Tile is on)");
  };

  if (!Attr(*tex_coords, "left").empty()) {
    if (frame->tile_x) {
      report_tiling_conflict("horiz");
    } else if (auto left = CheckedTexCoordAttr(
                   *tex_coords, "left", *frame, "TexCoords", diagnostics,
                   &ok);
               left.has_value()) {
      quad.upper_left.u = *left;
      quad.lower_left.u = *left;
    }
  }
  if (!Attr(*tex_coords, "right").empty()) {
    if (frame->tile_x) {
      report_tiling_conflict("horiz");
    } else if (auto right = CheckedTexCoordAttr(
                   *tex_coords, "right", *frame, "TexCoords", diagnostics,
                   &ok);
               right.has_value()) {
      quad.upper_right.u = *right;
      quad.lower_right.u = *right;
    }
  }
  if (!Attr(*tex_coords, "top").empty()) {
    if (frame->tile_y) {
      report_tiling_conflict("vert");
    } else if (auto top = CheckedTexCoordAttr(
                   *tex_coords, "top", *frame, "TexCoords", diagnostics,
                   &ok);
               top.has_value()) {
      quad.upper_left.v = *top;
      quad.upper_right.v = *top;
    }
  }
  if (!Attr(*tex_coords, "bottom").empty()) {
    if (frame->tile_y) {
      report_tiling_conflict("vert");
    } else if (auto bottom = CheckedTexCoordAttr(
                   *tex_coords, "bottom", *frame, "TexCoords", diagnostics,
                   &ok);
               bottom.has_value()) {
      quad.lower_left.v = *bottom;
      quad.lower_right.v = *bottom;
    }
  }

  if (!ok) {
    return;
  }

  frame->tex_coords = quad;
  frame->tex_left = quad.upper_left.u;
  frame->tex_top = quad.upper_left.v;
  frame->tex_right = quad.upper_right.u;
  frame->tex_bottom = quad.lower_left.v;
  frame->has_tex_coords = true;
}

void ParseTextureVisualChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }

  if (const auto* color = FirstDirectChild(node, "Color"); color != nullptr) {
    frame->has_vertex_color = true;
    frame->color_r = std::clamp(FloatAttr(*color, "r").value_or(1.0F), 0.0F, 1.0F);
    frame->color_g = std::clamp(FloatAttr(*color, "g").value_or(1.0F), 0.0F, 1.0F);
    frame->color_b = std::clamp(FloatAttr(*color, "b").value_or(1.0F), 0.0F, 1.0F);
    frame->color_a = std::clamp(FloatAttr(*color, "a").value_or(1.0F), 0.0F, 1.0F);
  }

  const auto* gradient = FirstDirectChild(node, "Gradient");
  if (gradient == nullptr) {
    return;
  }

  frame->gradient.enabled = true;
  const auto orientation = ToLowerAscii(Attr(*gradient, "orientation"));
  frame->gradient.orientation = orientation == "vertical"
                                    ? TextureGradientOrientation::kVertical
                                    : TextureGradientOrientation::kHorizontal;

  if (const auto* min_color = FirstDirectChild(*gradient, "MinColor"); min_color != nullptr) {
    frame->gradient.min_color.r =
        std::clamp(FloatAttr(*min_color, "r").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.min_color.g =
        std::clamp(FloatAttr(*min_color, "g").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.min_color.b =
        std::clamp(FloatAttr(*min_color, "b").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.min_color.a =
        std::clamp(FloatAttr(*min_color, "a").value_or(1.0F), 0.0F, 1.0F);
  }

  if (const auto* max_color = FirstDirectChild(*gradient, "MaxColor"); max_color != nullptr) {
    frame->gradient.max_color.r =
        std::clamp(FloatAttr(*max_color, "r").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.max_color.g =
        std::clamp(FloatAttr(*max_color, "g").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.max_color.b =
        std::clamp(FloatAttr(*max_color, "b").value_or(0.0F), 0.0F, 1.0F);
    frame->gradient.max_color.a =
        std::clamp(FloatAttr(*max_color, "a").value_or(1.0F), 0.0F, 1.0F);
  }
}

void ParseFontStringVisualChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }

  frame->font_reference = Trim(Attr(node, "font"));
  frame->font_outline = Trim(Attr(node, "outline"));
  frame->font_monochrome = OptionalBoolAttr(node, "monochrome");

  if (const auto* height = FirstDirectChild(node, "FontHeight");
      height != nullptr) {
    float stored_height = 0.0F;
    if (openwow::ui::xml::RelValue_ref(height, &stored_height, nullptr) != 0) {
      frame->text_height_stored = stored_height;
      frame->has_font_height = true;
    }
  }

  if (const auto* color = FirstDirectChild(node, "Color"); color != nullptr) {
    frame->has_text_color = true;
    frame->color_r = std::clamp(FloatAttr(*color, "r").value_or(0.0F), 0.0F, 1.0F);
    frame->color_g = std::clamp(FloatAttr(*color, "g").value_or(0.0F), 0.0F, 1.0F);
    frame->color_b = std::clamp(FloatAttr(*color, "b").value_or(0.0F), 0.0F, 1.0F);
    frame->color_a = std::clamp(FloatAttr(*color, "a").value_or(1.0F), 0.0F, 1.0F);
  }

  const auto* shadow = FirstDirectChild(node, "Shadow");
  if (shadow == nullptr) {
    return;
  }
  frame->has_text_shadow = true;
  if (const auto* color = FirstDirectChild(*shadow, "Color"); color != nullptr) {
    frame->text_shadow_r =
        std::clamp(FloatAttr(*color, "r").value_or(0.0F), 0.0F, 1.0F);
    frame->text_shadow_g =
        std::clamp(FloatAttr(*color, "g").value_or(0.0F), 0.0F, 1.0F);
    frame->text_shadow_b =
        std::clamp(FloatAttr(*color, "b").value_or(0.0F), 0.0F, 1.0F);
    frame->text_shadow_a =
        std::clamp(FloatAttr(*color, "a").value_or(1.0F), 0.0F, 1.0F);
  }
  if (const auto* offset = FirstDirectChild(*shadow, "Offset");
      offset != nullptr) {
    (void)openwow::ui::xml::RelDimension_ref(
        offset, &frame->text_shadow_x, &frame->text_shadow_y, nullptr);
  }
}

void ParseModelVisualChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  if (const auto sequence = FirstIntAttr(node, {"sequence", "sequenceID", "anim"})) {
    frame->model_sequence = std::max(0, *sequence);
    frame->has_model_sequence = true;
  }
  if (const auto camera = FirstIntAttr(node, {"camera", "cameraIndex", "cameraID"})) {
    frame->model_camera = std::max(0, *camera);
    frame->has_model_camera = true;
  }
  if (const auto sequence_time =
          FirstIntAttr(node, {"sequenceTime", "sequenceTimeMs", "animationTime"})) {
    frame->model_sequence_time_ms = static_cast<std::uint32_t>(std::max(0, *sequence_time));
    frame->has_model_sequence_time = true;
  }
  if (const auto facing = FirstFloatAttr(node, {"facing", "facingRad", "modelFacing"})) {
    frame->model_facing_rad = *facing;
    frame->has_model_facing = true;
  }
  if (const auto x = FirstFloatAttr(node, {"modelX", "positionX", "posX"});
      x.has_value()) {
    frame->model_x = *x;
    frame->model_y = FirstFloatAttr(node, {"modelY", "positionY", "posY"}).value_or(0.0F);
    frame->model_z = FirstFloatAttr(node, {"modelZ", "positionZ", "posZ"}).value_or(0.0F);
    frame->has_model_position = true;
  }
  if (const auto* position = FirstDirectChild(node, "Position"); position != nullptr) {
    if (const auto x = FloatAttr(*position, "x"); x.has_value()) {
      frame->model_x = *x;
      frame->model_y = FloatAttr(*position, "y").value_or(0.0F);
      frame->model_z = FloatAttr(*position, "z").value_or(0.0F);
      frame->has_model_position = true;
    }
  }
  if (const auto* facing = FirstDirectChild(node, "Facing"); facing != nullptr) {
    if (const auto value = FirstFloatAttr(*facing, {"radians", "value", "angle"});
        value.has_value()) {
      frame->model_facing_rad = *value;
      frame->has_model_facing = true;
    }
  }
  if (const auto* sequence = FirstDirectChild(node, "Sequence"); sequence != nullptr) {
    if (const auto id = FirstIntAttr(*sequence, {"id", "index", "value"}); id.has_value()) {
      frame->model_sequence = std::max(0, *id);
      frame->has_model_sequence = true;
    }
    if (const auto time = FirstIntAttr(*sequence, {"time", "timeMs", "sequenceTime"});
        time.has_value()) {
      frame->model_sequence_time_ms = static_cast<std::uint32_t>(std::max(0, *time));
      frame->has_model_sequence_time = true;
    }
  }
  if (const auto* camera = FirstDirectChild(node, "Camera"); camera != nullptr) {
    if (const auto id = FirstIntAttr(*camera, {"id", "index", "value"}); id.has_value()) {
      frame->model_camera = std::max(0, *id);
      frame->has_model_camera = true;
    }
  }
  const auto* fog_color = FirstDirectChild(node, "FogColor");
  if (fog_color == nullptr) {
    return;
  }
  frame->fog_r = std::clamp(FloatAttr(*fog_color, "r").value_or(1.0F), 0.0F, 1.0F);
  frame->fog_g = std::clamp(FloatAttr(*fog_color, "g").value_or(1.0F), 0.0F, 1.0F);
  frame->fog_b = std::clamp(FloatAttr(*fog_color, "b").value_or(1.0F), 0.0F, 1.0F);
  frame->has_fog_color = true;
}

detail::TextInsets ParseTextInsets(const XmlNode& node) {
  detail::TextInsets out;
  const auto* text_insets = FirstDirectChild(node, "TextInsets");
  if (text_insets == nullptr) {
    return out;
  }

  const auto* values = FirstDirectChild(*text_insets, "AbsInset");
  if (values == nullptr) {
    values = text_insets;
  }
  out.left = IntAttr(*values, "left").value_or(0);
  out.right = IntAttr(*values, "right").value_or(0);
  out.top = IntAttr(*values, "top").value_or(0);
  out.bottom = IntAttr(*values, "bottom").value_or(0);
  out.ok = true;
  return out;
}

std::vector<UiFrame::InitialAttribute> ParseInitialAttributes(const XmlNode& node) {
  std::vector<UiFrame::InitialAttribute> attributes;
  const auto* attrs_node = FirstDirectChild(node, "Attributes");
  if (attrs_node == nullptr) {
    return attributes;
  }

  for (const auto* attr_node : DirectChildren(*attrs_node, "Attribute")) {
    UiFrame::InitialAttribute attribute;
    attribute.name = Attr(*attr_node, "name");
    if (attribute.name.empty()) {
      continue;
    }
    attribute.type = ToLowerAscii(Attr(*attr_node, "type"));
    if (attribute.type.empty()) {
      attribute.type = "string";
    }
    attribute.value = Attr(*attr_node, "value");
    if (attribute.type != "nil" && attribute.value.empty()) {
      continue;
    }
    attributes.push_back(std::move(attribute));
  }

  return attributes;
}

UiColor ParseUiColor(const XmlNode& node) {
  return UiColor{
      .r = std::clamp(FloatAttr(node, "r").value_or(1.0F), 0.0F, 1.0F),
      .g = std::clamp(FloatAttr(node, "g").value_or(1.0F), 0.0F, 1.0F),
      .b = std::clamp(FloatAttr(node, "b").value_or(1.0F), 0.0F, 1.0F),
      .a = std::clamp(FloatAttr(node, "a").value_or(1.0F), 0.0F, 1.0F),
  };
}

void ParseHitRectInsets(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  const auto* insets = FirstDirectChild(node, "HitRectInsets");
  if (insets == nullptr) {
    return;
  }
  const auto* abs = FirstDirectChild(*insets, "AbsInset");
  if (abs == nullptr) {
    return;
  }
  frame->hit_rect_inset_left = FloatAttr(*abs, "left").value_or(0.0F);
  frame->hit_rect_inset_right = FloatAttr(*abs, "right").value_or(0.0F);
  frame->hit_rect_inset_top = FloatAttr(*abs, "top").value_or(0.0F);
  frame->hit_rect_inset_bottom = FloatAttr(*abs, "bottom").value_or(0.0F);
  frame->has_hit_rect_insets = true;
}

void ParseButtonStateChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  if (const auto* font = FirstDirectChild(node, "NormalFont"); font != nullptr) {
    frame->button_normal_font_style = Trim(Attr(*font, "style"));
  }
  if (const auto* font = FirstDirectChild(node, "DisabledFont"); font != nullptr) {
    frame->button_disabled_font_style = Trim(Attr(*font, "style"));
  }
  if (const auto* font = FirstDirectChild(node, "HighlightFont"); font != nullptr) {
    frame->button_highlight_font_style = Trim(Attr(*font, "style"));
  }
  if (const auto* color = FirstDirectChild(node, "NormalColor"); color != nullptr) {
    frame->button_normal_color = ParseUiColor(*color);
  }
  if (const auto* color = FirstDirectChild(node, "DisabledColor"); color != nullptr) {
    frame->button_disabled_color = ParseUiColor(*color);
  }
  if (const auto* color = FirstDirectChild(node, "HighlightColor"); color != nullptr) {
    frame->button_highlight_color = ParseUiColor(*color);
  }
}

void ParseStatusBarChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  if (!frame->status_bar.has_value()) {
    frame->status_bar.emplace();
  }
  auto& definition = *frame->status_bar;
  if (const auto* texture = FirstDirectChild(node, "BarTexture"); texture != nullptr) {
    definition.texture_path = Attr(*texture, "file");
  }
  if (const auto* color = FirstDirectChild(node, "BarColor"); color != nullptr) {
    const auto parsed = ParseUiColor(*color);
    definition.color = openwow::ui::widgets::StatusBarColor{
        .red = parsed.r,
        .green = parsed.g,
        .blue = parsed.b,
        .alpha = parsed.a,
    };
  }
}

void ParseColorSelectChildren(const XmlNode& node, UiFrame* frame) {
  if (frame == nullptr) {
    return;
  }
  if (const auto* texture = FirstDirectChild(node, "ColorWheelTexture"); texture != nullptr) {
    frame->color_wheel_texture_file = Attr(*texture, "file");
  }
  if (const auto* texture = FirstDirectChild(node, "ColorWheelThumbTexture");
      texture != nullptr) {
    frame->color_wheel_thumb_texture_file = Attr(*texture, "file");
  }
  if (const auto* texture = FirstDirectChild(node, "ColorValueTexture"); texture != nullptr) {
    frame->color_value_texture_file = Attr(*texture, "file");
  }
  if (const auto* texture = FirstDirectChild(node, "ColorValueThumbTexture");
      texture != nullptr) {
    frame->color_value_thumb_texture_file = Attr(*texture, "file");
  }
}

}

namespace detail {

bool IsTextureRegionTag(std::string_view tag) {
  const std::string lower = openwow::text::ToLowerAscii(std::string(tag));
  return lower.size() >= 7 && lower.rfind("texture") == (lower.size() - 7);
}

bool IsRuntimeWidgetTag(std::string_view tag) {
  const std::string lower = openwow::text::ToLowerAscii(std::string(tag));
  if (lower == "frame" || lower == "button" || lower == "checkbutton" ||
      lower == "editbox" || lower == "messageframe" || lower == "scrollframe" ||
      lower == "scrollingmessageframe" || lower == "slider" || lower == "simplehtml" ||
      lower == "statusbar" || lower == "colorselect" || lower == "model" ||
      lower == "modelffx" || lower == "playermodel" || lower == "dressupmodel" ||
      lower == "movieframe" || lower == "texture" || lower == "fontstring" ||
      lower == "buttontext" || lower == "questpoiframe" || lower == "cooldown" ||
      lower == "minimap" || lower == "gametooltip" || lower == "worldframe" ||
      lower == "tabardmodel") {
    return true;
  }
  return IsTextureRegionTag(lower);
}

std::string NormalizeWidgetKind(std::string_view tag) {
  std::string kind = openwow::text::Trim(std::string(tag));
  const std::string lower = openwow::text::ToLowerAscii(kind);
  if (lower == "buttontext") {
    return "FontString";
  }
  if (lower == "modelffx") {
    return "ModelFFX";
  }
  if (lower == "playermodel") {
    return "PlayerModel";
  }
  if (lower == "dressupmodel") {
    return "DressUpModel";
  }
  if (lower != "texture" && IsTextureRegionTag(lower)) {
    return "Texture";
  }
  return kind;
}

std::string CanonicalizeScriptEvent(std::string_view authored_name) {
  return openwow::ui::CanonicalizeUiScriptHandlerName(authored_name);
}

}

namespace {

std::vector<ScriptHandler> ParseScriptHandlers(const XmlNode& node) {
  std::vector<ScriptHandler> handlers;
  const auto* scripts = FirstDirectChild(node, "Scripts");
  if (scripts == nullptr) {
    return handlers;
  }

  for (const auto& child : scripts->children) {
    if (child.tag.empty()) {
      continue;
    }
    ScriptHandler handler;

    handler.event = detail::CanonicalizeScriptEvent(child.tag);
    handler.function = Attr(child, "function");
    handler.body = Trim(child.text);
    handlers.push_back(std::move(handler));
  }

  return handlers;
}

bool IsAnimChildTag(const std::string& lower) {
  return lower == "animation" || lower == "alpha" || lower == "scale" ||
         lower == "translation" || lower == "rotation" || lower == "path";
}

std::string CapitalizeFirst(std::string value) {
  if (!value.empty()) {
    value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
  }
  return value;
}

void ParseAnimationOrigin(const XmlNode& node,
                          UiAnimation* anim,
                          std::vector<std::string>* diagnostics) {
  if (anim == nullptr) {
    return;
  }

  const auto* origin = FirstDirectChild(node, "Origin");
  if (origin == nullptr) {
    if (anim->origin_point.empty()) {
      anim->origin_point = "CENTER";
    }
    return;
  }

  const std::string raw_point = Attr(*origin, "point");
  if (raw_point.empty()) {
    anim->origin_point = "CENTER";
  } else {
    int point_value = 0;
    if (openwow::ui::StringToFramePoint(raw_point.c_str(), &point_value) != 0) {
      anim->origin_point = openwow::ui::FramePointToString(point_value);
    } else {
      anim->origin_point = "CENTER";
      if (diagnostics != nullptr) {
        diagnostics->push_back("Invalid origin point " + raw_point + " in element Origin");
      }
    }
  }

  const auto* offset = FirstDirectChild(*origin, "Offset");
  if (offset == nullptr) {
    return;
  }
  if (auto x = FloatAttr(*offset, "x"); x.has_value()) {
    anim->origin_x = *x;
  }
  if (auto y = FloatAttr(*offset, "y"); y.has_value()) {
    anim->origin_y = *y;
  }
}

void ParseAnimationControlPoints(const XmlNode& node,
                                 UiAnimation* anim,
                                 std::vector<std::string>* diagnostics) {
  if (anim == nullptr) {
    return;
  }

  const auto* control_points = FirstDirectChild(node, "ControlPoints");
  if (control_points == nullptr) {
    return;
  }

  for (const auto& child : control_points->children) {
    const std::string lower = ToLowerAscii(child.tag);
    if (lower != "controlpoint") {
      if (diagnostics != nullptr) {
        const std::string name = anim->name.empty() ? "<unnamed>" : anim->name;
        diagnostics->push_back("Path " + name +
                               ": Unknown child node in ControlPoints element: " + child.tag);
      }
      continue;
    }

    UiPathControlPoint point;
    point.name = Attr(child, "name");
    point.inherits = Attr(child, "inherits");
    point.parent_key = Attr(child, "parentKey");
    point.offset_x = FloatAttr(child, "offsetX");
    point.offset_y = FloatAttr(child, "offsetY");
    anim->control_points.push_back(std::move(point));
  }
}

void ParseScaleAxis(const XmlNode& node,
                    const char* attr_name,
                    const char* diagnostic_name,
                    std::optional<float>* stock_value,
                    const UiAnimation& anim,
                    std::vector<std::string>* diagnostics) {
  const auto parsed = FloatAttr(node, attr_name);
  if (!parsed.has_value()) {
    return;
  }
  *stock_value = *parsed;
  if (*parsed < 0.001F && diagnostics != nullptr) {
    const std::string name = anim.name.empty() ? "<unnamed>" : anim.name;
    diagnostics->push_back(name + ": Invalid " + diagnostic_name + " value: " +
                           Attr(node, attr_name) + ". Value must be at least 0.001.");
  }
}

std::vector<UiAnimationGroup> ParseAnimationGroups(const XmlNode& node,
                                                   std::vector<std::string>* diagnostics) {
  std::vector<UiAnimationGroup> groups;
  const auto* animations = FirstDirectChild(node, "Animations");
  if (animations == nullptr) {
    return groups;
  }

  for (const auto* group_node : DirectChildren(*animations, "AnimationGroup")) {
    UiAnimationGroup group;
    group.name = Attr(*group_node, "name");
    group.inherits = Attr(*group_node, "inherits");
    group.parent_key = Attr(*group_node, "parentKey");
    group.looping = Attr(*group_node, "looping");
    group.initial_offset_x = FloatAttr(*group_node, "initialOffsetX").value_or(0.0F);
    group.initial_offset_y = FloatAttr(*group_node, "initialOffsetY").value_or(0.0F);
    group.script_handlers = ParseScriptHandlers(*group_node);

    for (const auto& child : group_node->children) {
      const std::string lower = ToLowerAscii(child.tag);
      if (!IsAnimChildTag(lower)) {
        continue;
      }

      UiAnimation anim;
      anim.type = lower == "animation" ? "Animation" : CapitalizeFirst(lower);
      anim.name = Attr(child, "name");
      anim.duration = FloatAttr(child, "duration");
      anim.start_delay = FloatAttr(child, "startDelay");
      anim.end_delay = FloatAttr(child, "endDelay");
      anim.max_framerate = FloatAttr(child, "maxFramerate");
      anim.order = IntAttr(child, "order");
      anim.smoothing = Attr(child, "smoothing");
      anim.parent_key = Attr(child, "parentKey");
      anim.inherits = Attr(child, "inherits");

      const std::string raw_change = Attr(child, "change");
      if (!raw_change.empty()) {
        const auto parsed_change =
            openwow::ui::anim::ParseAlphaXmlChangeAttribute(raw_change.c_str(), anim.name);
        if (parsed_change.has_value) {
          anim.change = parsed_change.value;
        }
        if (diagnostics != nullptr && !parsed_change.warning.empty()) {
          diagnostics->push_back(parsed_change.warning);
        }
      }
      anim.from_alpha = FloatAttr(child, "fromAlpha");
      anim.to_alpha = FloatAttr(child, "toAlpha");

      ParseScaleAxis(child, "scaleX", "scaleX", &anim.stock_scale_x, anim, diagnostics);
      ParseScaleAxis(child, "scaleY", "scaleY", &anim.stock_scale_y, anim, diagnostics);
      anim.from_scale_x = FloatAttr(child, "fromScaleX");
      anim.from_scale_y = FloatAttr(child, "fromScaleY");
      if (auto to_x = FloatAttr(child, "toScaleX"); to_x.has_value()) {
        anim.to_scale_x = *to_x;
      }
      if (auto to_y = FloatAttr(child, "toScaleY"); to_y.has_value()) {
        anim.to_scale_y = *to_y;
      }

      anim.offset_x = FloatAttr(child, "offsetX");
      anim.offset_y = FloatAttr(child, "offsetY");
      anim.degrees = FloatAttr(child, "degrees");
      anim.radians = FloatAttr(child, "radians");

      anim.origin_point = Attr(child, "origin");
      anim.origin_x = FloatAttr(child, "originx");
      anim.origin_y = FloatAttr(child, "originy");
      if (anim.origin_point.empty()) {
        ParseAnimationOrigin(child, &anim, diagnostics);
      }

      if (lower == "path") {
        const std::string curve = Attr(child, "curve");
        if (!curve.empty()) {
          int curve_value = 0;
          if (openwow::ui::ParseCurveTypeString(curve.c_str(), &curve_value) != 0) {
            anim.curve_type = curve;
          } else if (diagnostics != nullptr) {
            const std::string name = anim.name.empty() ? "<unnamed>" : anim.name;
            diagnostics->push_back("Path " + name + ": Invalid curve value: " + curve);
          }
        }
        ParseAnimationControlPoints(child, &anim, diagnostics);
      }

      anim.script_handlers = ParseScriptHandlers(child);
      group.animations.push_back(std::move(anim));
    }

    groups.push_back(std::move(group));
  }

  return groups;
}

std::optional<float> ParseBackdropValueElement(const XmlNode& backdrop,
                                               std::string_view element_name) {
  const auto* element = FirstDirectChild(backdrop, element_name);
  if (element == nullptr) {
    return std::nullopt;
  }
  if (auto value = FloatAttr(*element, "val"); value.has_value()) {
    return value;
  }
  if (const auto* abs = FirstDirectChild(*element, "AbsValue"); abs != nullptr) {
    return FloatAttr(*abs, "val").value_or(0.0F);
  }
  if (const auto* rel = FirstDirectChild(*element, "RelValue"); rel != nullptr) {
    return FloatAttr(*rel, "val").value_or(0.0F);
  }
  return std::nullopt;
}

std::optional<detail::BackdropSpec> ParseBackdrop(const XmlNode& node) {
  const auto* backdrop = FirstDirectChild(node, "Backdrop");
  if (backdrop == nullptr) {
    return std::nullopt;
  }

  detail::BackdropSpec spec;
  spec.bg_file = Attr(*backdrop, "bgFile");
  spec.edge_file = Attr(*backdrop, "edgeFile");
  spec.tile = OptionalBoolAttr(*backdrop, "tile").value_or(false);
  spec.alpha_mode = Attr(*backdrop, "alphaMode");

  if (const auto* insets = FirstDirectChild(*backdrop, "BackgroundInsets"); insets != nullptr) {
    const XmlNode* values = insets;
    if (const auto* abs = FirstDirectChild(*insets, "AbsInset"); abs != nullptr) {
      values = abs;
    } else if (const auto* rel = FirstDirectChild(*insets, "RelInset"); rel != nullptr) {
      values = rel;
    }
    spec.inset_left = FloatAttr(*values, "left").value_or(0.0F);
    spec.inset_right = FloatAttr(*values, "right").value_or(0.0F);
    spec.inset_top = FloatAttr(*values, "top").value_or(0.0F);
    spec.inset_bottom = FloatAttr(*values, "bottom").value_or(0.0F);
  }

  if (const auto* color = FirstDirectChild(*backdrop, "Color"); color != nullptr) {
    spec.bg_color_r = std::clamp(FloatAttr(*color, "r").value_or(1.0F), 0.0F, 1.0F);
    spec.bg_color_g = std::clamp(FloatAttr(*color, "g").value_or(1.0F), 0.0F, 1.0F);
    spec.bg_color_b = std::clamp(FloatAttr(*color, "b").value_or(1.0F), 0.0F, 1.0F);
    spec.bg_color_a = std::clamp(FloatAttr(*color, "a").value_or(1.0F), 0.0F, 1.0F);
    spec.has_bg_color = true;
  }
  if (const auto* color = FirstDirectChild(*backdrop, "BorderColor"); color != nullptr) {
    spec.border_color_r = std::clamp(FloatAttr(*color, "r").value_or(1.0F), 0.0F, 1.0F);
    spec.border_color_g = std::clamp(FloatAttr(*color, "g").value_or(1.0F), 0.0F, 1.0F);
    spec.border_color_b = std::clamp(FloatAttr(*color, "b").value_or(1.0F), 0.0F, 1.0F);
    spec.border_color_a = std::clamp(FloatAttr(*color, "a").value_or(1.0F), 0.0F, 1.0F);
    spec.has_border_color = true;
  }

  if (const auto tile_size = ParseBackdropValueElement(*backdrop, "TileSize");
      tile_size.has_value()) {
    spec.tile_size = *tile_size;
  }
  if (const auto edge_size = ParseBackdropValueElement(*backdrop, "EdgeSize");
      edge_size.has_value()) {
    spec.edge_size = *edge_size;
  }

  return spec;
}

UiFrame* FindOwnedTextRegion(ParserContext* ctx, const std::string_view owner,
                             const UiFrame::RegionRole role) {
  if (ctx == nullptr || owner.empty()) {
    return nullptr;
  }
  const auto region = std::find_if(
      ctx->result.frames.begin(), ctx->result.frames.end(),
      [&](const UiFrame& candidate) {
        return candidate.parent == owner && candidate.region_role == role;
      });
  return region != ctx->result.frames.end() ? &*region : nullptr;
}

void FinalizeWidget(ParserContext* ctx, const XmlNode& node, std::size_t index) {
  if (ctx == nullptr || index >= ctx->result.frames.size()) {
    return;
  }

  auto& frame = ctx->result.frames[index];
  const std::string parent_token_name =
      ctx->stack.empty() ? std::string{}
                         : ctx->stack.back().attribute_parent_token_name;
  const auto anchors = ParseAnchors(node, &ctx->result.diagnostics);
  if (!anchors.empty()) {
    frame.anchors = anchors;
    for (auto& anchor : frame.anchors) {
      if (anchor.relative_to.empty()) {
        if (!frame.parent.empty()) {
          anchor.relative_to = frame.parent;
        }
      } else if (!parent_token_name.empty()) {
        anchor.relative_to =
            detail::ResolveParentToken(anchor.relative_to, parent_token_name);
      }
    }
  }
  FillDimensions(&frame, node);
  const std::string frame_kind_lower = ToLowerAscii(frame.kind);
  if (frame_kind_lower == "texture") {
    if (const auto tile = OptionalBoolAttr(node, "horizTile"); tile.has_value()) {
      frame.tile_x = *tile;
      frame.tile_x_explicit = true;
    }
    if (const auto tile = OptionalBoolAttr(node, "vertTile"); tile.has_value()) {
      frame.tile_y = *tile;
      frame.tile_y_explicit = true;
    }
  }
  FillTexCoords(&frame, node, &ctx->result.diagnostics);
  ParseHitRectInsets(node, &frame);

  if (frame_kind_lower == "texture") {
    ParseTextureVisualChildren(node, &frame);
    if (const auto alpha = FloatAttr(node, "alpha"); alpha.has_value()) {
      float clamped = 0.0F;
      if (*alpha >= 1.0F) {
        clamped = 1.0F;
      } else if (*alpha >= 0.0F) {
        clamped = *alpha;
      }
      const auto packed = static_cast<std::uint8_t>(clamped * 255.0F);
      frame.texture_alpha = static_cast<float>(packed) / 255.0F;
    }
    const std::string original_tag_lower = ToLowerAscii(node.tag);
    const bool slider_thumb = original_tag_lower == "thumbtexture" &&
                              !frame.parent.empty() &&
                              ctx->index_by_name.contains(frame.parent) &&
                              ToLowerAscii(ctx->result.frames[
                                  ctx->index_by_name.at(frame.parent)].kind) == "slider";
    if (slider_thumb && frame.anchors.empty()) {

      frame.anchors = {UiAnchor{
          .point = "CENTER",
          .relative_to = frame.parent,
          .relative_point = "CENTER",
      }};
    }
    if (!slider_thumb && !frame.set_all_points && frame.anchors.empty() &&
        !frame.parent.empty()) {

      frame.set_all_points = true;
    }
  }
  if (frame_kind_lower == "model" || frame_kind_lower == "modelffx") {
    ParseModelVisualChildren(node, &frame);
  }
  if (frame_kind_lower == "button" || frame_kind_lower == "checkbutton") {
    ParseButtonStateChildren(node, &frame);
  }
  if (frame_kind_lower == "statusbar") {
    ParseStatusBarChildren(node, &frame);
  }
  if (frame_kind_lower == "colorselect") {
    ParseColorSelectChildren(node, &frame);
  }
  if (frame_kind_lower == "fontstring") {
    ParseFontStringVisualChildren(node, &frame);
    if (frame.text.empty()) {
      frame.text = FirstDirectChildText(node, "Text");
    }
    if (frame.justify_h.empty()) {
      frame.justify_h = FirstDirectChildText(node, "JustifyH");
    }
    if (frame.justify_v.empty()) {
      frame.justify_v = FirstDirectChildText(node, "JustifyV");
    }
  }

  if (frame_kind_lower == "frame" && frame.anchors.empty() && !frame.set_all_points &&
      !frame.width.has_value() && !frame.height.has_value() &&
      IsGeneratedAnonymousName(frame.name)) {
    frame.set_all_points = true;
  }

  if (frame_kind_lower == "fontstring" && frame.anchors.empty() &&
      !frame.set_all_points &&
      frame.region_role == UiFrame::RegionRole::EditBoxText) {
    frame.set_all_points = true;
  }

  if (frame.anchors.empty() && !frame.set_all_points && !frame.parent.empty()) {
    const auto parent_it = ctx->index_by_name.find(frame.parent);
    if (parent_it != ctx->index_by_name.end() && parent_it->second < ctx->result.frames.size()) {
      const auto& parent = ctx->result.frames[parent_it->second];
      if (ToLowerAscii(parent.kind) == "scrollframe") {
        frame.anchors = {
            UiAnchor{
                .point = "TOPLEFT",
                .relative_to = frame.parent,
                .relative_point = "TOPLEFT",
                .x = 0.0F,
                .y = 0.0F,
            },
        };
      }
    }
  }

  if (frame_kind_lower == "editbox" && !frame.name.empty()) {
    const auto insets = ParseTextInsets(node);
    if (insets.ok) {
      frame.text_inset_left = insets.left;
      frame.text_inset_right = insets.right;
      frame.text_inset_top = insets.top;
      frame.text_inset_bottom = insets.bottom;
      frame.has_text_insets = true;

      if (auto* text_frame = FindOwnedTextRegion(
              ctx, frame.name, UiFrame::RegionRole::EditBoxText);
          text_frame != nullptr && text_frame->anchors.empty()) {
          text_frame->set_all_points = false;
          text_frame->anchors = {
              UiAnchor{
                  .point = "TOPLEFT",
                  .relative_to = frame.name,
                  .relative_point = "TOPLEFT",
                  .x = static_cast<float>(insets.left),
                  .y = static_cast<float>(-insets.top),
              },
              UiAnchor{
                  .point = "BOTTOMRIGHT",
                  .relative_to = frame.name,
                  .relative_point = "BOTTOMRIGHT",
                  .x = static_cast<float>(-insets.right),
                  .y = static_cast<float>(insets.bottom),
              },
          };
      }
    }
  }

  if ((frame_kind_lower == "button" || frame_kind_lower == "checkbutton") && !frame.name.empty()) {
    if (const auto* normal_font = FirstDirectChild(node, "NormalFont"); normal_font != nullptr) {
      const std::string style = frame.button_normal_font_style;
      if (!style.empty()) {
        if (auto* text_frame = FindOwnedTextRegion(
                ctx, frame.name, UiFrame::RegionRole::ButtonText);
            text_frame != nullptr) {
          text_frame->font_style = style;
          if (frame.button_normal_color.has_value()) {
            text_frame->color_r = frame.button_normal_color->r;
            text_frame->color_g = frame.button_normal_color->g;
            text_frame->color_b = frame.button_normal_color->b;
            text_frame->color_a = frame.button_normal_color->a;
          }
        }
      }
    }
  }

  if (const auto backdrop = ParseBackdrop(node); backdrop.has_value()) {
    ctx->result.frames[index].backdrop = *backdrop;
    detail::InjectBackdropPieces(*backdrop, frame, &ctx->index_by_name, &ctx->result.frames);
  }

  auto& frame_post = ctx->result.frames[index];
  auto anim_groups = ParseAnimationGroups(node, &ctx->result.diagnostics);
  if (!anim_groups.empty()) {
    frame_post.animation_groups = std::move(anim_groups);
  }
  auto scripts = ParseScriptHandlers(node);
  if (!scripts.empty()) {
    frame_post.script_handlers = std::move(scripts);
  }
  auto attributes = ParseInitialAttributes(node);
  if (!attributes.empty()) {
    frame_post.initial_attributes = std::move(attributes);
  }
}

std::size_t BeginWidget(ParserContext* ctx, const XmlNode& node) {
  const std::string parent_name = CurrentTokenParentName(*ctx);
  const std::string structural_parent_name =
      CurrentStructuralParentName(*ctx);
  const std::string parent_kind = CurrentTokenParentKind(*ctx);
  const std::string explicit_parent_attr = Attr(node, "parent");
  const std::string effective_parent_name =
      explicit_parent_attr.empty()
          ? parent_name
          : detail::ResolveParentToken(explicit_parent_attr, parent_name);

  std::string kind = detail::NormalizeWidgetKind(node.tag);
  const std::string lower_kind = ToLowerAscii(kind);
  const std::string original_name_attr = Attr(node, "name");
  const bool has_explicit_name = !Trim(original_name_attr).empty();
  std::string name =
      detail::SanitizeWidgetIdentifier(detail::ResolveParentToken(original_name_attr,
                                                                  effective_parent_name));
  const std::string diagnostic_frame_name = name;
  const std::string original_tag_lower = ToLowerAscii(node.tag);

  if (name.empty()) {

    const std::string &generated_parent_name =
        structural_parent_name.empty() ? parent_name : structural_parent_name;
    if (name.empty() && lower_kind == "texture") {
      if (!generated_parent_name.empty() && original_tag_lower != "texture" &&
          detail::IsTextureRegionTag(original_tag_lower)) {
        name = generated_parent_name + Trim(node.tag);
      }
      if (name.empty()) {
        const std::string suffix =
            "__Texture" + std::to_string(++ctx->unnamed_texture_counter);
        name = generated_parent_name.empty()
                   ? suffix
                   : (generated_parent_name + "." + suffix);
      }
    } else if (name.empty() && lower_kind == "fontstring") {
      const std::string suffix =
          "__FontString" + std::to_string(++ctx->unnamed_fontstring_counter);
      name = generated_parent_name.empty()
                 ? suffix
                 : (generated_parent_name + "." + suffix);
    } else if (name.empty()) {
      const std::string suffix = "__Anon" + kind + std::to_string(++ctx->unnamed_widget_counter);
      name = generated_parent_name.empty()
                 ? suffix
                 : (generated_parent_name + "." + suffix);
    }
  }

  std::optional<std::size_t> top_level_group_index;
  if (ctx->stack.empty()) {
    top_level_group_index = ctx->result.top_level_groups.size();
    ctx->result.top_level_groups.push_back({
        .first_frame = ctx->result.frames.size(),
        .frame_count = 0,
    });
  }

  std::string runtime_name = name;
  for (std::size_t suffix = ctx->result.frames.size(); ctx->index_by_name.contains(runtime_name);
       ++suffix) {
    runtime_name = name + ".__FrameXML" + std::to_string(suffix);
  }

  UiFrame frame;
  frame.kind = kind;
  frame.authored_xml = std::make_shared<XmlNode>(node);
  frame.name = std::move(runtime_name);
  frame.lua_name = has_explicit_name ? name : std::string{};
  frame.publish_to_lua = has_explicit_name;
  frame.top_level = ctx->stack.empty();
  frame.parent = explicit_parent_attr.empty()
                     ? structural_parent_name
                     : detail::ResolveParentToken(explicit_parent_attr, parent_name);
  if (const auto parent_key = Attr(node, "parentKey"); !parent_key.empty()) {
    frame.parent_keys.push_back(parent_key);
  }
  frame.inherits =
      detail::ResolveParentToken(detail::NormalizeInherits(Attr(node, "inherits")), parent_name);
  frame.file = Attr(node, "file");
  if (ToLowerAscii(frame.kind) == "questpoiframe") {
    frame.quest_poi_fill_texture = Attr(node, "filltexture");
    frame.quest_poi_border_texture = Attr(node, "bordertexture");
  }
  frame.text = Attr(node, "text");
  frame.font_style = detail::ResolveParentToken(Trim(Attr(node, "inherits")), parent_name);
  if (ToLowerAscii(frame.kind) == "fontstring") {
    if (original_tag_lower == "buttontext") {
      frame.region_role = UiFrame::RegionRole::ButtonText;
    } else if (ToLowerAscii(parent_kind) == "editbox" && ctx->layer_stack.empty()) {
      frame.region_role = UiFrame::RegionRole::EditBoxText;
    } else if ((ToLowerAscii(parent_kind) == "messageframe" ||
                ToLowerAscii(parent_kind) == "scrollingmessageframe") &&
               ctx->layer_stack.empty()) {
      frame.region_role = UiFrame::RegionRole::MessageFontDefinition;
    }
    frame.font_style = frame.inherits;
    frame.justify_h = Attr(node, "justifyH");
    frame.justify_v = Attr(node, "justifyV");
    frame.word_wrap = OptionalBoolAttr(node, "wordwrap");
    frame.non_space_wrap = OptionalBoolAttr(node, "nonspacewrap");
    frame.indented_word_wrap = OptionalBoolAttr(node, "indented");
    if (const auto spacing = FloatAttr(node, "spacing"); spacing.has_value()) {
      frame.text_spacing_stored = openwow::ui::PixelUiHorizontalCoordinateToStored(*spacing);
      frame.has_text_spacing = true;
    }
    if (const auto max_lines = IntAttr(node, "maxLines"); max_lines.has_value()) {
      frame.max_lines = std::max(0, *max_lines);
    }
    if (original_tag_lower == "buttontext") {
      frame.draw_layer = "OVERLAY";
      frame.draw_sublevel = 0;
    } else if (frame.region_role == UiFrame::RegionRole::EditBoxText) {
      frame.draw_layer = "OVERLAY";
      frame.draw_sublevel = 0;
    } else if (!parent_name.empty() && name == (parent_name + "Text")) {
      const std::string parent_lower = ToLowerAscii(parent_kind);
      if (parent_lower == "editbox" || parent_lower == "button" || parent_lower == "checkbutton") {
        frame.draw_layer = "OVERLAY";
        frame.draw_sublevel = 0;
      }
    }
  }
  if (ToLowerAscii(frame.kind) == "texture") {
    using TextureRole = UiFrame::TextureRole;
    if (original_tag_lower == "normaltexture") {
      frame.texture_role = TextureRole::ButtonNormal;
    } else if (original_tag_lower == "pushedtexture") {
      frame.texture_role = TextureRole::ButtonPushed;
    } else if (original_tag_lower == "disabledtexture") {
      frame.texture_role = TextureRole::ButtonDisabled;
    } else if (original_tag_lower == "highlighttexture") {
      frame.texture_role = TextureRole::ButtonHighlight;
    } else if (original_tag_lower == "checkedtexture") {
      frame.texture_role = TextureRole::CheckButtonChecked;
    } else if (original_tag_lower == "disabledcheckedtexture") {
      frame.texture_role = TextureRole::CheckButtonDisabledChecked;
    } else if (original_tag_lower == "thumbtexture") {
      frame.texture_role = TextureRole::SliderThumb;
    } else if (original_tag_lower == "bartexture") {
      frame.texture_role = TextureRole::StatusBarFill;
    }
  }
  if (const std::string draw_layer =
          ToUpperAscii(Trim(Attr(node, "drawLayer")));
      !draw_layer.empty()) {
    frame.draw_layer = draw_layer;
  }
  frame.alpha_mode = Attr(node, "alphaMode");
  if (const auto fog_near = FloatAttr(node, "fogNear"); fog_near.has_value()) {
    frame.fog_near = std::max(0.0F, *fog_near);
    frame.has_fog_near = true;
  }
  if (const auto fog_far = FloatAttr(node, "fogFar"); fog_far.has_value()) {
    frame.fog_far = std::max(0.0F, *fog_far);
    frame.has_fog_far = true;
  }
  if (ToLowerAscii(frame.kind) == "modelffx") {
    if (const auto glow = FloatAttr(node, "glow"); glow.has_value()) {
      frame.glow = *glow;
      frame.has_glow = true;
    }
  }
  if (IsModelLikeKind(frame.kind)) {
    if (const auto model_scale = FloatAttr(node, "scale");
        model_scale.has_value() && *model_scale > 0.0F) {
      frame.model_scale = *model_scale;
      frame.has_model_scale = true;
    }
  }
  if ((ToLowerAscii(frame.kind) == "texture" || ToLowerAscii(frame.kind) == "fontstring") &&
      !ctx->layer_stack.empty()) {
    frame.draw_layer = ctx->layer_stack.back().level;
    frame.draw_sublevel = ctx->layer_stack.back().sublevel;
  } else if (ToLowerAscii(frame.kind) == "texture") {
    if (original_tag_lower == "bartexture" && !ctx->stack.empty()) {
      const auto owner_index = ctx->stack.back().index;
      if (owner_index < ctx->result.frames.size() &&
          !ctx->result.frames[owner_index].draw_layer.empty()) {
        frame.draw_layer = ctx->result.frames[owner_index].draw_layer;
      }
    } else if (original_tag_lower == "normaltexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "ARTWORK";
    } else if (original_tag_lower == "pushedtexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "ARTWORK";
      frame.draw_sublevel = 1;
    } else if (original_tag_lower == "disabledtexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "ARTWORK";
      frame.draw_sublevel = 2;
    } else if (original_tag_lower == "highlighttexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "HIGHLIGHT";
    } else if (original_tag_lower == "checkedtexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "ARTWORK";
      frame.draw_sublevel = 1;
    } else if (original_tag_lower == "disabledcheckedtexture" && frame.draw_layer.empty()) {
      frame.draw_layer = "ARTWORK";
      frame.draw_sublevel = 2;
    }
  }
  frame.frame_strata = NormalizeScriptFrameStrata(Attr(node, "frameStrata"), diagnostic_frame_name,
                                                  &ctx->result.diagnostics);
  if (const auto id = IntAttr(node, "id"); id.has_value()) {
    frame.id = *id;
    frame.has_id = true;
  }
  if (const auto level = IntAttr(node, "frameLevel"); level.has_value()) {
    frame.frame_level = *level;
    frame.has_frame_level = true;
  }
  if (const auto depth = FloatAttr(node, "depth"); depth.has_value()) {
    frame.depth = *depth;
  }
  if (const auto toplevel = OptionalBoolAttr(node, "toplevel");
      toplevel.has_value()) {
    frame.toplevel = *toplevel;
    frame.toplevel_explicit = true;
  }
  frame.width = FloatAttr(node, "width");
  frame.height = FloatAttr(node, "height");
  if (const auto hidden = OptionalBoolAttr(node, "hidden"); hidden.has_value()) {
    frame.visible = !*hidden;
    frame.visibility_explicit = true;
  }
  frame.scroll_child_content = ctx->scroll_child_depth > 0;
  frame.scroll_child_membership_count = ctx->scroll_child_depth;
  if (const auto protected_frame = OptionalBoolAttr(node, "protected");
      protected_frame.has_value()) {
    frame.protected_frame = *protected_frame;
    frame.protected_explicit = true;
  }
  frame.movable = OptionalBoolAttr(node, "movable");
  frame.resizable = OptionalBoolAttr(node, "resizable");
  if (ToLowerAscii(frame.kind) == "fontstring" && ToLowerAscii(parent_kind) == "simplehtml") {
    frame.visible = false;
    frame.visibility_explicit = true;
  }
  if (ToLowerAscii(frame.kind) == "editbox") {
    frame.password = BoolAttr(node, "password");
    frame.auto_focus = OptionalBoolAttr(node, "autoFocus");
    if (const auto letters = IntAttr(node, "letters"); letters.has_value()) {
      frame.max_letters = std::max(-1, *letters);
    }
  }
  if (ToLowerAscii(frame.kind) == "statusbar") {
    if (!frame.status_bar.has_value()) {
      frame.status_bar.emplace();
    }
    auto& definition = *frame.status_bar;
    definition.minimum = FloatAttr(node, "minValue");
    definition.maximum = FloatAttr(node, "maxValue");
    definition.default_value = FloatAttr(node, "defaultValue");
    if (definition.minimum.has_value() && definition.maximum.has_value()) {
      switch (openwow::ui::widgets::ValidateStatusBarRange(
          *definition.minimum, *definition.maximum)) {
        case openwow::ui::widgets::StatusBarRangeError::EndpointOutOfRange:
          ctx->result.diagnostics.push_back(
              "Frame " + frame.name + ": Min or Max out of range");
          break;
        case openwow::ui::widgets::StatusBarRangeError::SpanTooLarge:
          ctx->result.diagnostics.push_back(
              "Frame " + frame.name + ": Min and Max too far apart");
          break;
        case openwow::ui::widgets::StatusBarRangeError::None:
          break;
      }
    }
    const std::string authored_orientation = Trim(Attr(node, "orientation"));
    const std::string orientation = ToUpperAscii(authored_orientation);
    if (orientation.empty()) {
      definition.orientation.kind =
          openwow::ui::widgets::ParsedStatusBarOrientationKind::Unspecified;
    } else if (orientation == "HORIZONTAL") {
      definition.orientation.kind =
          openwow::ui::widgets::ParsedStatusBarOrientationKind::Horizontal;
    } else if (orientation == "VERTICAL") {
      definition.orientation.kind =
          openwow::ui::widgets::ParsedStatusBarOrientationKind::Vertical;
    } else {
      definition.orientation.kind =
          openwow::ui::widgets::ParsedStatusBarOrientationKind::Invalid;
      definition.orientation.invalid_token = authored_orientation;
      ctx->result.diagnostics.push_back(
          "Frame " + frame.name + ": Unknown orientation " +
          authored_orientation + " in element " + node.tag);
    }
    definition.rotates_texture = OptionalBoolAttr(node, "rotatesTexture");
  }
  if (ToLowerAscii(frame.kind) == "messageframe" ||
      ToLowerAscii(frame.kind) == "scrollingmessageframe") {
    frame.message_font_style = Trim(Attr(node, "font"));
    frame.message_fading = OptionalBoolAttr(node, "fade");
    if (const auto duration = FloatAttr(node, "displayDuration");
        duration.has_value() && *duration > 0.0F) {
      frame.message_display_duration = *duration;
    }
    if (const auto duration = FloatAttr(node, "fadeDuration");
        duration.has_value() && *duration > 0.0F) {
      frame.message_fade_duration = *duration;
    }
    if (const auto lines = IntAttr(node, "maxLines");
        lines.has_value() && *lines > 0) {
      frame.message_max_lines = *lines;
    }
    frame.message_insert_mode = ToUpperAscii(Trim(Attr(node, "insertMode")));
  }
  if (ToLowerAscii(frame.kind) == "texture") {
    if (original_tag_lower == "normaltexture" || original_tag_lower == "pushedtexture" ||
        original_tag_lower == "disabledtexture" || original_tag_lower == "highlighttexture" ||
        original_tag_lower == "checkedtexture" || original_tag_lower == "disabledcheckedtexture") {
      frame.visible = true;
      frame.visibility_explicit = true;
    }
  }
  frame.virtual_template = BoolAttr(node, "virtual");
  if (const auto enabled = OptionalBoolAttr(node, "enableMouse");
      enabled.has_value()) {
    frame.enable_mouse = *enabled;
    frame.enable_mouse_explicit = true;
  }
  if (const auto enabled = OptionalBoolAttr(node, "enableKeyboard");
      enabled.has_value()) {
    frame.enable_keyboard = *enabled;
    frame.enable_keyboard_explicit = true;
  }
  if (const auto set_all_points = OptionalBoolAttr(node, "setAllPoints");
      set_all_points.has_value()) {
    frame.set_all_points = *set_all_points;
    frame.set_all_points_explicit = true;
  }
  frame.clamped_to_screen = OptionalBoolAttr(node, "clampedToScreen");
  if (lower_kind == "cooldown") {

    frame.cooldown_reverse = OptionalBoolAttr(node, "reverse");
    frame.cooldown_draw_edge = OptionalBoolAttr(node, "drawEdge");
  }

  const std::size_t index = ctx->result.frames.size();
  ctx->index_by_name.insert_or_assign(frame.name, index);
  ctx->result.frames.push_back(std::move(frame));

  const std::string token_parent_name = has_explicit_name ? name : parent_name;
  const std::string token_parent_kind =
      has_explicit_name ? ctx->result.frames[index].kind : parent_kind;
  ctx->stack.push_back(ParentContext{
      .index = index,
      .attribute_parent_token_name = parent_name,
      .token_parent_name = token_parent_name,
      .token_parent_kind = token_parent_kind,
      .top_level_group_index = top_level_group_index,
  });
  return index;
}

void WalkChildren(ParserContext *ctx, const XmlNode &node) {
  if (ctx == nullptr) {
    return;
  }
  for (const auto& child : node.children) {
    const std::string lower = ToLowerAscii(child.tag);
    if (lower == "layer") {
      ctx->layer_stack.push_back(LayerContext{
          .level = Attr(child, "level"),
          .sublevel = IntAttr(child, "subLevel").value_or(0),
      });
      WalkChildren(ctx, child);
      ctx->layer_stack.pop_back();
      continue;
    }

    if (lower == "scrollchild") {
      ++ctx->scroll_child_depth;
      WalkChildren(ctx, child);
      --ctx->scroll_child_depth;
      continue;
    }

    if (!detail::IsRuntimeWidgetTag(child.tag)) {
      WalkChildren(ctx, child);
      continue;
    }

    const std::size_t index = BeginWidget(ctx, child);
    WalkChildren(ctx, child);
    FinalizeWidget(ctx, child, index);
    if (ctx->stack.back().top_level_group_index.has_value()) {
      auto& group = ctx->result.top_level_groups[*ctx->stack.back().top_level_group_index];
      group.frame_count = ctx->result.frames.size() - group.first_frame;
    }
    ctx->stack.pop_back();
  }
}

}

ParseResult ParseFrameXml(const std::string& xml_text) {
  xml::XMLNode root;
  std::string error;
  if (!xml::FrameXMLParser::ParseDocument(xml_text, &root, &error)) {
    ParserContext ctx;
    ctx.result.error = error.empty() ? "Failed to parse XML document" : error;
    return ctx.result;
  }

  return ParseFrameXml(root);
}

ParseResult ParseFrameXml(const xml::XMLNode& root) {
  ParserContext ctx;

  if (ToLowerAscii(root.tag) != "ui") {
    ctx.result.error = "missing <Ui root>";
    return ctx.result;
  }

  WalkChildren(&ctx, root);
  ctx.result.ok = true;
  return std::move(ctx.result);
}

void RegisterVirtualTemplate(const UiFrame& frame) {
  if (frame.name.empty()) return;
  VirtualTemplateRegistry().insert_or_assign(frame.name, frame);
}

VirtualTemplateRegistrySnapshot CaptureVirtualTemplates() {
  return VirtualTemplateRegistry();
}

void RestoreVirtualTemplates(VirtualTemplateRegistrySnapshot snapshot) {
  VirtualTemplateRegistry() = std::move(snapshot);
}

const UiFrame* GetVirtualTemplate(const std::string& name) {
  const auto& registry = VirtualTemplateRegistry();
  const auto it = registry.find(name);
  return it != registry.end() ? &it->second : nullptr;
}

const UiAnimationGroup* GetVirtualAnimationGroupTemplate(const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  const auto& registry = VirtualTemplateRegistry();
  for (const auto& [_, frame] : registry) {
    for (const auto& group : frame.animation_groups) {
      if (group.name == name) {
        return &group;
      }
    }
  }

  return nullptr;
}

const UiAnimation* GetVirtualAnimationTemplate(const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  const auto& registry = VirtualTemplateRegistry();
  for (const auto& [_, frame] : registry) {
    for (const auto& group : frame.animation_groups) {
      for (const auto& animation : group.animations) {
        if (animation.name == name) {
          return &animation;
        }
      }
    }
  }

  return nullptr;
}

const UiPathControlPoint* GetVirtualControlPointTemplate(const std::string& name) {
  if (name.empty()) {
    return nullptr;
  }

  const auto& registry = VirtualTemplateRegistry();
  for (const auto& [_, frame] : registry) {
    for (const auto& group : frame.animation_groups) {
      for (const auto& animation : group.animations) {
        for (const auto& point : animation.control_points) {
          if (point.name == name) {
            return &point;
          }
        }
      }
    }
  }

  return nullptr;
}

void ClearVirtualTemplates() {
  VirtualTemplateRegistry().clear();
}

}

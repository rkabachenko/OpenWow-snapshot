
#include "openwow/ui/widgets/simple_texture.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/widgets/widget_xml_helpers.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/ui/xml/xml_value_helpers.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>

namespace openwow::ui::widgets {

namespace {

const char *TextureDisplayName(const CSimpleTexture &texture) {
  return texture.GetDisplayName();
}

bool TryParseBlendModeName(const char *value, BlendMode *out_mode) {
  if (value == nullptr || out_mode == nullptr) {
    return false;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(value, "DISABLE")) {
    *out_mode = BlendMode::Disable;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "BLEND")) {
    *out_mode = BlendMode::Blend;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "ALPHAKEY")) {
    *out_mode = BlendMode::Alphakey;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "ADD")) {
    *out_mode = BlendMode::Add;
    return true;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "MOD")) {
    *out_mode = BlendMode::Mod;
    return true;
  }

  return false;
}

float ClampUnitRange(const float value) noexcept {
  return std::clamp(value, 0.0f, 1.0f);
}

TextureGradientColor ParseGradientColor(const openwow::ui::xml::Color &color) noexcept {
  return TextureGradientColor{color.r, color.g, color.b, color.a};
}

void ParseTextureColorNode(const openwow::ui::xml::XMLNode &node,
                           TextureGradientColor *out_color) {
  if (out_color == nullptr) {
    return;
  }

  out_color->r = ClampUnitRange(node.GetAttrFloat("r", 0.0f));
  out_color->g = ClampUnitRange(node.GetAttrFloat("g", 0.0f));
  out_color->b = ClampUnitRange(node.GetAttrFloat("b", 0.0f));
  out_color->a = ClampUnitRange(node.GetAttrFloat("a", 1.0f));
}

bool ValidateTexCoordValue(float value) noexcept {
  return value >= -10000.0f && value <= 10000.0f;
}

const char *FindNodeAttributeValue(const openwow::ui::xml::XMLNode &node,
                                   const char *name) {
  if (name == nullptr || *name == '\0') {
    return nullptr;
  }

  const std::string key(name);
  auto it = node.attributes.find(key);
  if (it != node.attributes.end()) {
    return it->second.empty() ? nullptr : it->second.c_str();
  }

  const auto lower_key = openwow::text::ToLowerAscii(key);
  for (const auto &[candidate, value] : node.attributes) {
    if (openwow::text::ToLowerAscii(candidate) == lower_key) {
      return value.empty() ? nullptr : value.c_str();
    }
  }

  return nullptr;
}

}

void CSimpleTexture::LoadXML(const openwow::ui::xml::XMLFrameDef &frame_def,
                             openwow::ui::xml::ErrorContext *error_handler) {
  std::unordered_set<std::string> inheritance_stack;
  (void)LoadXMLWithInheritance(frame_def, error_handler, &inheritance_stack);
}

bool CSimpleTexture::LoadXMLWithInheritance(
    const openwow::ui::xml::XMLFrameDef &frame_def,
    openwow::ui::xml::ErrorContext *error_handler,
    std::unordered_set<std::string> *inheritance_stack) {
  for (const auto &template_name : openwow::ui::framexml::SplitTemplateList(
           frame_def.inherits,
           openwow::ui::framexml::TemplateListSyntax::kCommaSeparated)) {
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
    const bool inherited =
        LoadXMLWithInheritance(*template_def, error_handler, inheritance_stack);
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

  if (const char *horiz_tile = FindAttributeValue(frame_def, "horizTile");
      horiz_tile != nullptr && *horiz_tile != '\0') {
    SetHorizTile(ScriptParseBoolStringOrDefault(horiz_tile, false));
  }

  if (const char *vert_tile = FindAttributeValue(frame_def, "vertTile");
      vert_tile != nullptr && *vert_tile != '\0') {
    SetVertTile(ScriptParseBoolStringOrDefault(vert_tile, false));
  }

  bool has_color_texture = false;
  TextureGradientColor color_texture{};

  for (const auto &child : frame_def.raw_node.children) {
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Color")) {
      ParseTextureColorNode(child, &color_texture);
      has_color_texture = true;
      SetColorTexture(color_texture.r, color_texture.g, color_texture.b,
                      color_texture.a);
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "Gradient")) {
      std::uint32_t orientation = 0;
      openwow::ui::xml::Color min_color;
      openwow::ui::xml::Color max_color;
      if (openwow::ui::xml::MaxColor_ref(&child, &orientation, &min_color,
                                         &max_color, error_handler) != 0) {
        SetGradient(
            orientation == 0 ? TextureGradientOrientation::Horizontal
                             : TextureGradientOrientation::Vertical,
            ParseGradientColor(min_color), ParseGradientColor(max_color));
      }
      continue;
    }

    if (!openwow::text::EqualsIgnoreCaseAscii(child.tag, "TexCoords")) {
      continue;
    }

    TextureCoordQuad tex_coords;
    bool tex_coords_valid = true;

    if (const auto *rect = child.FindChild("Rect"); rect != nullptr) {
      TextureCoordQuad parsed_rect;
      parsed_rect.upperLeft.u = rect->GetAttrFloat("ULx", 0.0f);
      parsed_rect.upperLeft.v = rect->GetAttrFloat("ULy", 0.0f);
      parsed_rect.lowerLeft.u = rect->GetAttrFloat("LLx", 0.0f);
      parsed_rect.lowerLeft.v = rect->GetAttrFloat("LLy", 1.0f);
      parsed_rect.upperRight.u = rect->GetAttrFloat("URx", 1.0f);
      parsed_rect.upperRight.v = rect->GetAttrFloat("URy", 0.0f);
      parsed_rect.lowerRight.u = rect->GetAttrFloat("LRx", 1.0f);
      parsed_rect.lowerRight.v = rect->GetAttrFloat("LRy", 1.0f);
      if (ValidateTexCoordValue(parsed_rect.upperLeft.u) &&
          ValidateTexCoordValue(parsed_rect.upperLeft.v) &&
          ValidateTexCoordValue(parsed_rect.lowerLeft.u) &&
          ValidateTexCoordValue(parsed_rect.lowerLeft.v) &&
          ValidateTexCoordValue(parsed_rect.upperRight.u) &&
          ValidateTexCoordValue(parsed_rect.upperRight.v) &&
          ValidateTexCoordValue(parsed_rect.lowerRight.u) &&
          ValidateTexCoordValue(parsed_rect.lowerRight.v)) {
        tex_coords = parsed_rect;
      } else if (error_handler != nullptr) {
        tex_coords_valid = false;
        error_handler->ReportError(
            "Texture %s: Invalid rect value (out of range)",
            TextureDisplayName(*this));
      } else {
        tex_coords_valid = false;
      }
    }

    if (const auto *left_value = FindNodeAttributeValue(child, "left");
        left_value != nullptr && *left_value != '\0') {
      const float left =
          static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(left_value));
      if (GetHorizTile()) {
        if (error_handler != nullptr) {
          error_handler->ReportError(
              "Texture %s: Invalid TexCoords value (horizTile is on)",
              TextureDisplayName(*this));
        }
      } else if (ValidateTexCoordValue(left)) {
        tex_coords.upperLeft.u = left;
        tex_coords.lowerLeft.u = left;
      } else if (error_handler != nullptr) {
        tex_coords_valid = false;
        error_handler->ReportError(
            "Texture %s: Invalid TexCoords value (out of range)",
            TextureDisplayName(*this));
      } else {
        tex_coords_valid = false;
      }
    }

    if (const char *right_value = FindNodeAttributeValue(child, "right");
        right_value != nullptr && *right_value != '\0') {
      const float right =
          static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(right_value));
      if (GetHorizTile()) {
        if (error_handler != nullptr) {
          error_handler->ReportError(
              "Texture %s: Invalid TexCoords value (horizTile is on)",
              TextureDisplayName(*this));
        }
      } else if (ValidateTexCoordValue(right)) {
        tex_coords.upperRight.u = right;
        tex_coords.lowerRight.u = right;
      } else if (error_handler != nullptr) {
        tex_coords_valid = false;
        error_handler->ReportError(
            "Texture %s: Invalid TexCoords value (out of range)",
            TextureDisplayName(*this));
      } else {
        tex_coords_valid = false;
      }
    }

    if (const char *top_value = FindNodeAttributeValue(child, "top");
        top_value != nullptr && *top_value != '\0') {
      const float top =
          static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(top_value));
      if (GetVertTile()) {
        if (error_handler != nullptr) {
          error_handler->ReportError(
              "Texture %s: Invalid TexCoords value (vertTile is on)",
              TextureDisplayName(*this));
        }
      } else if (ValidateTexCoordValue(top)) {
        tex_coords.upperLeft.v = top;
        tex_coords.upperRight.v = top;
      } else if (error_handler != nullptr) {
        tex_coords_valid = false;
        error_handler->ReportError(
            "Texture %s: Invalid TexCoords value (out of range)",
            TextureDisplayName(*this));
      } else {
        tex_coords_valid = false;
      }
    }

    if (const char *bottom_value = FindNodeAttributeValue(child, "bottom");
        bottom_value != nullptr && *bottom_value != '\0') {
      const float bottom =
          static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(bottom_value));
      if (GetVertTile()) {
        if (error_handler != nullptr) {
          error_handler->ReportError(
              "Texture %s: Invalid TexCoords value (vertTile is on)",
              TextureDisplayName(*this));
        }
      } else if (ValidateTexCoordValue(bottom)) {
        tex_coords.lowerLeft.v = bottom;
        tex_coords.lowerRight.v = bottom;
      } else if (error_handler != nullptr) {
        tex_coords_valid = false;
        error_handler->ReportError(
            "Texture %s: Invalid TexCoords value (out of range)",
            TextureDisplayName(*this));
      } else {
        tex_coords_valid = false;
      }
    }

    if (tex_coords_valid) {
      SetTexCoordQuad(tex_coords);
    }
  }

  if (const char *file = FindAttributeValue(frame_def, "file");
      file != nullptr && *file != '\0') {
    SetTexture(file);
    if (!texture_asset()) {
      if (error_handler != nullptr) {
        error_handler->ReportError("Texture %s: Unable to load texture file %s",
                                   TextureDisplayName(*this), file);
      }
      SetColorTexture(0.0f, 1.0f, 0.0f, 1.0f);
    } else if (has_color_texture) {
      SetVertexColor(color_texture.r, color_texture.g, color_texture.b,
                     color_texture.a);
    }
  }

  if (const char *alpha_mode = FindAttributeValue(frame_def, "alphaMode");
      alpha_mode != nullptr && *alpha_mode != '\0') {
    BlendMode mode = BlendMode::Blend;
    if (TryParseBlendModeName(alpha_mode, &mode)) {
      SetBlendMode(mode);
    }
  }

  if (const char *alpha = FindAttributeValue(frame_def, "alpha");
      alpha != nullptr && *alpha != '\0') {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    GetVertexColor(r, g, b, a);
    SetVertexColor(
        r, g, b,
        std::clamp(static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(alpha)),
                   0.0f,
                   1.0f));
  }

  if (const char *non_blocking = FindAttributeValue(frame_def, "nonBlocking");
      non_blocking != nullptr && *non_blocking != '\0') {
    SetNonBlocking(ScriptParseBoolStringOrDefault(non_blocking, false));
  }

  if (GetNumPoints() == 0) {
    auto *parent_region = GetParent();
    if (parent_region != nullptr) {
      SetAllPoints(parent_region);
    }
  }
  return true;
}

}

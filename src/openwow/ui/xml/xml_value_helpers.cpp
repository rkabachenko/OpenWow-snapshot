
#include "openwow/ui/xml/xml_value_helpers.h"

#include "openwow/core/cimvector.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cstring>

namespace openwow::ui::xml {

using openwow::text::ToLowerAscii;

float AbsToNDC(const float pixels) {
  return openwow::ui::PixelUiHorizontalCoordinateToStored(pixels);
}

static bool TagEq(const std::string& a, const char* b) {
  return ToLowerAscii(a) == ToLowerAscii(std::string(b));
}

static bool OrientationStringToEnum(const std::string& s, uint32_t* out) {
  const auto lower = ToLowerAscii(s);
  if (lower == "horizontal") { *out = 0; return true; }
  if (lower == "vertical")   { *out = 1; return true; }
  return false;
}

static bool StringToFramePoint(const std::string& s, uint32_t* out) {
  if (s.empty() || !out) return false;

  const auto upper = [&]() {
    std::string u = s;
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u;
  }();

  if (upper == "TOPLEFT")     { *out = 0; return true; }
  if (upper == "TOP")         { *out = 1; return true; }
  if (upper == "TOPRIGHT")    { *out = 2; return true; }
  if (upper == "LEFT")        { *out = 3; return true; }
  if (upper == "CENTER")      { *out = 4; return true; }
  if (upper == "RIGHT")       { *out = 5; return true; }
  if (upper == "BOTTOMLEFT")  { *out = 6; return true; }
  if (upper == "BOTTOM")      { *out = 7; return true; }
  if (upper == "BOTTOMRIGHT") { *out = 8; return true; }

  return false;
}

static void ParseColorAttrs(const XMLNode* node, Color* color) {
  color->r = node->GetAttrFloat("r", color->r);
  color->g = node->GetAttrFloat("g", color->g);
  color->b = node->GetAttrFloat("b", color->b);
  color->a = node->GetAttrFloat("a", color->a);
}

int RelValue_ref(const XMLNode* node, float* out_val, ErrorContext* ctx) {
  *out_val = 0.0f;

  const bool has_val = node->HasAttr("val");
  if (has_val) {
    *out_val = AbsToNDC(node->GetAttrFloat("val"));
  }

  if (node->children.empty()) {
    if (has_val) return 1;
    if (ctx) ctx->ReportError("No \"val\" attribute in element");
    return 0;
  }

  const XMLNode& child = node->children.front();

  if (TagEq(child.tag, "AbsValue")) {
    *out_val = AbsToNDC(child.GetAttrFloat("val"));
    return 1;
  }

  if (TagEq(child.tag, "RelValue")) {
    *out_val = child.GetAttrFloat("val");
    return 1;
  }

  if (ctx)
    ctx->ReportError("Unknown child node in %s element: %s",
                     node->tag.c_str(), child.tag.c_str());
  return 0;
}

int RelDimension_ref(const XMLNode* node, float* out_x, float* out_y,
                     ErrorContext* ctx) {
  *out_x = 0.0f;
  *out_y = 0.0f;

  const bool has_x = node->HasAttr("x");
  const bool has_y = node->HasAttr("y");
  if (has_x) *out_x = AbsToNDC(node->GetAttrFloat("x"));
  if (has_y) *out_y = AbsToNDC(node->GetAttrFloat("y"));

  if (node->children.empty()) {
    return has_y ? 1 : 0;
  }

  const XMLNode& child = node->children.front();

  if (TagEq(child.tag, "AbsDimension")) {
    *out_x = AbsToNDC(child.GetAttrFloat("x"));
    *out_y = AbsToNDC(child.GetAttrFloat("y"));
    return 1;
  }

  if (TagEq(child.tag, "RelDimension")) {
    *out_x = child.GetAttrFloat("x");
    *out_y = child.GetAttrFloat("y");
    return 1;
  }

  if (ctx)
    ctx->ReportError("Unknown child node in %s element: %s",
                     node->tag.c_str(), child.tag.c_str());
  return 0;
}

int RelInset_ref(const XMLNode* node, float* left, float* right, float* top,
                 float* bottom, ErrorContext* ctx) {
  *left   = 0.0f;
  *right  = 0.0f;
  *top    = 0.0f;
  *bottom = 0.0f;

  const bool has_left   = node->HasAttr("left");
  const bool has_right  = node->HasAttr("right");
  const bool has_top    = node->HasAttr("top");
  const bool has_bottom = node->HasAttr("bottom");
  const bool has_any    = has_left || has_right || has_top || has_bottom;

  if (has_left)   *left   = AbsToNDC(node->GetAttrFloat("left"));
  if (has_right)  *right  = AbsToNDC(node->GetAttrFloat("right"));
  if (has_top)    *top    = AbsToNDC(node->GetAttrFloat("top"));
  if (has_bottom) *bottom = AbsToNDC(node->GetAttrFloat("bottom"));

  if (node->children.empty()) {
    if (has_any) return 1;
    if (ctx) ctx->ReportError("No inset attributes in element");
    return 0;
  }

  const XMLNode& child = node->children.front();

  if (TagEq(child.tag, "AbsInset")) {
    *left   = AbsToNDC(child.GetAttrFloat("left"));
    *right  = AbsToNDC(child.GetAttrFloat("right"));
    *top    = AbsToNDC(child.GetAttrFloat("top"));
    *bottom = AbsToNDC(child.GetAttrFloat("bottom"));
    return 1;
  }

  if (TagEq(child.tag, "RelInset")) {
    *left   = child.GetAttrFloat("left");
    *right  = child.GetAttrFloat("right");
    *top    = child.GetAttrFloat("top");
    *bottom = child.GetAttrFloat("bottom");
    return 1;
  }

  if (ctx)
    ctx->ReportError("Unknown child node in %s element: %s",
                     node->tag.c_str(), child.tag.c_str());
  return 0;
}

int XMLNode_ReadClampedColorToCImVector(const XMLNode* node,
                                        uint32_t* outColor) {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;

  if (node->HasAttr("r"))
    r = std::clamp(node->GetAttrFloat("r"), 0.0f, 1.0f);

  if (node->HasAttr("g"))
    g = std::clamp(node->GetAttrFloat("g"), 0.0f, 1.0f);

  if (node->HasAttr("b"))
    b = std::clamp(node->GetAttrFloat("b"), 0.0f, 1.0f);

  if (node->HasAttr("a"))
    a = std::clamp(node->GetAttrFloat("a"), 0.0f, 1.0f);

  core::PackArgbFloatsToBgra(*outColor, a, r, g, b);
  return 1;
}

int MaxColor_ref(const XMLNode* node, uint32_t* orientation, Color* min_color,
                 Color* max_color, ErrorContext* ctx) {
  *orientation = 0;

  if (node->HasAttr("orientation")) {
    const auto orient_str = node->GetAttr("orientation");
    if (!OrientationStringToEnum(orient_str, orientation)) {
      if (ctx)
        ctx->ReportError("Unknown orientation value: %s",
                         orient_str.c_str());
      return 0;
    }
  }

  for (const auto& child : node->children) {
    if (TagEq(child.tag, "MinColor")) {
      ParseColorAttrs(&child, min_color);
    } else if (TagEq(child.tag, "MaxColor")) {
      ParseColorAttrs(&child, max_color);
    }
  }

  return 1;
}

int fn_point(const XMLNode* node, uint32_t* out_point, float* out_offset_xy,
             ErrorContext* ctx) {
  *out_point      = 4;
  out_offset_xy[0] = 0.0f;
  out_offset_xy[1] = 0.0f;

  if (node->HasAttr("point")) {
    const auto point_str = node->GetAttr("point");
    if (!StringToFramePoint(point_str, out_point)) {
      if (ctx) {
        ctx->ReportError("Invalid origin point %s in element %s", point_str.c_str(),
                         node->tag.c_str());
      }
    }
  }

  const XMLNode* offset_child = node->FindChild("Offset");
  if (offset_child) {
    RelDimension_ref(offset_child, &out_offset_xy[0], &out_offset_xy[1], ctx);
  }

  return 1;
}

}

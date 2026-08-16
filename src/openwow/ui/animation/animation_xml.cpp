
#include "openwow/ui/animation/animation_xml.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/animation/alpha_anim.h"
#include "openwow/ui/animation/animation.h"
#include "openwow/ui/animation/animation_coordinate_space.h"
#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/animation/animation_types.h"
#include "openwow/ui/animation/path_anim.h"
#include "openwow/ui/animation/rotation_anim.h"
#include "openwow/ui/animation/scale_anim.h"
#include "openwow/ui/animation/translation_anim.h"
#include "openwow/ui/ui_enum_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openwow::ui::anim {

static bool IcaseEq(const char *a, const char *b) {
  if (!a || !b)
    return false;
  while (*a && *b) {
    if (std::tolower(static_cast<unsigned char>(*a)) !=
        std::tolower(static_cast<unsigned char>(*b)))
      return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

static float ParseFloat(const char *s) {
  return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(s));
}

static void ParseOriginOffsetNode(const XmlNode &origin_node, float *out_x, float *out_y) {
  if (out_x == nullptr || out_y == nullptr) {
    return;
  }

  for (const auto &child : origin_node.children) {
    if (!IcaseEq(child.tag.c_str(), "Offset")) {
      continue;
    }

    if (const char *x_attr = child.GetAttr("x")) {
      *out_x = ParseFloat(x_attr);
    }
    if (const char *y_attr = child.GetAttr("y")) {
      *out_y = ParseFloat(y_attr);
    }
    return;
  }
}

template <typename AnimT>
static void ApplyAnimationOriginChildren(AnimT *anim, const XmlNode &node) {
  if (anim == nullptr) {
    return;
  }

  for (const auto &child : node.children) {
    if (!IcaseEq(child.tag.c_str(), "Origin")) {
      continue;
    }

    const char *point = child.GetAttr("point");
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    ParseOriginOffsetNode(child, &offset_x, &offset_y);
    anim->SetOriginPixels(point ? point : "CENTER", offset_x, offset_y);
  }
}

int NormalizeXmlAnimationOrder(int xml_order) {
  return std::clamp(xml_order - 1, 0, 99);
}

bool TryParseAnimationSmoothingString(const char* value,
                                      float* smooth_in,
                                      float* smooth_out,
                                      AnimCurveType* curve) {
  if (value == nullptr || smooth_in == nullptr || smooth_out == nullptr) {
    return false;
  }
  if (!ui::SmoothingStringToFloats(value, smooth_in, smooth_out)) {
    return false;
  }
  if (curve != nullptr) {
    if (*smooth_in >= 0.001f) {
      *curve = (*smooth_out >= 0.001f) ? AnimCurveType::InOut : AnimCurveType::In;
    } else {
      *curve = (*smooth_out >= 0.001f) ? AnimCurveType::Out : AnimCurveType::None;
    }
  }
  return true;
}

void ApplyXmlAnimationBaseAttributes(Animation* anim,
                                     std::optional<float> duration,
                                     std::optional<float> start_delay,
                                     std::optional<float> end_delay,
                                     std::optional<int> xml_order,
                                     const char* smoothing,
                                     std::optional<float> max_framerate) {
  if (anim == nullptr) {
    return;
  }

  if (duration.has_value()) {
    anim->SetDuration(*duration, false);
  }

  if (start_delay.has_value()) {
    anim->SetStartDelay(*start_delay, false);
  }

  if (end_delay.has_value()) {
    anim->SetEndDelay(*end_delay, false);
  }

  if (xml_order.has_value()) {
    anim->SetOrder(NormalizeXmlAnimationOrder(*xml_order), false);
  }

  if (max_framerate.has_value()) {
    anim->SetMaxFramerate(*max_framerate);
  }

  if (smoothing != nullptr && *smoothing != '\0') {
    float smooth_in = 0.0f;
    float smooth_out = 0.0f;
    if (TryParseAnimationSmoothingString(smoothing, &smooth_in, &smooth_out)) {
      anim->SetSmoothControlPoint(smooth_in, smooth_out);
    }
  }
}

static const ScriptSlot* FindScriptSlot(
    const openwow::ui::UiScriptHandlerOwner owner,
    const char* handler_name) {
  if (handler_name == nullptr || *handler_name == '\0') {
    return nullptr;
  }
  return openwow::ui::LookupUiScriptHandlerVariant(owner, handler_name);
}

const ScriptSlot* GetAnimGroupScriptSlot(const char* handler_name) {
  return FindScriptSlot(openwow::ui::UiScriptHandlerOwner::AnimationGroup,
                        handler_name);
}

const char* NormalizeAnimGroupScriptHandler(const char* handler_name) {
  const auto* slot = GetAnimGroupScriptSlot(handler_name);
  return slot != nullptr ? slot->canonical_name : nullptr;
}

const ScriptSlot* GetAnimScriptSlot(const char* handler_name) {
  return FindScriptSlot(openwow::ui::UiScriptHandlerOwner::Animation,
                        handler_name);
}

const char* NormalizeAnimScriptHandler(const char* handler_name) {
  const auto* slot = GetAnimScriptSlot(handler_name);
  return slot != nullptr ? slot->canonical_name : nullptr;
}

template <typename AnimT, typename LoaderT>
void LoadCreatedAnimationXML(AnimT *anim, const XmlNode &node, LoaderT &&loader) {
  if (!anim) {
    return;
  }

  anim->MarkLoadedFromXml();

  if (const char *name = node.GetAttr("name"); name && *name) {
    anim->SetName(name);
  }

  loader(anim, node);
  FinalizeLoadedAnimation(anim);
}

void FinalizeLoadedAnimation(Animation* anim) {
  if (anim == nullptr) {
    return;
  }

  anim->FinalizeXmlLoad();
}

static std::optional<float> GetOptionalFloatAttr(const XmlNode& node, const char* name) {
  if (const char* value = node.GetAttr(name); value && *value) {
    return ParseFloat(value);
  }
  return std::nullopt;
}

static std::optional<int> GetOptionalIntAttr(const XmlNode& node, const char* name) {
  if (const char* value = node.GetAttr(name); value && *value) {
    return std::atoi(value);
  }
  return std::nullopt;
}

static void LoadBaseAnimXML(Animation *anim, const XmlNode &node) {

  ApplyXmlAnimationBaseAttributes(anim,
                                  GetOptionalFloatAttr(node, "duration"),
                                  GetOptionalFloatAttr(node, "startDelay"),
                                  GetOptionalFloatAttr(node, "endDelay"),
                                  GetOptionalIntAttr(node, "order"),
                                  node.GetAttr("smoothing"),
                                  GetOptionalFloatAttr(node, "maxFramerate"));
}

static void LoadAnimationXML(Animation *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  for (const auto &child : node.children) {
    if (IcaseEq(child.tag.c_str(), "Scripts")) {

    }
  }
}

void LoadTranslationXML(TranslationAnim *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  float ox = 0.0f, oy = 0.0f;
  const char *attr_x = node.GetAttr("offsetX");
  if (attr_x && *attr_x)
    ox = ParseFloat(attr_x);

  const char *attr_y = node.GetAttr("offsetY");
  if (attr_y && *attr_y)
    oy = ParseFloat(attr_y);

  anim->SetOffset(ox, oy);

  for (const auto &child : node.children) {
    if (IcaseEq(child.tag.c_str(), "Scripts")) {

    }
  }
}

void LoadRotationXML(RotationAnim *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  const char *degrees = node.GetAttr("degrees");
  if (degrees && *degrees) {
    anim->SetDegrees(ParseFloat(degrees));
  }

  const char *radians = node.GetAttr("radians");
  if (radians && *radians) {
    anim->SetRadians(ParseFloat(radians));
  }

  ApplyAnimationOriginChildren(anim, node);
}

void LoadScaleXML(ScaleAnim *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  float sx = 0.0f, sy = 0.0f;
  const char *attr_sx = node.GetAttr("scaleX");
  if (attr_sx && *attr_sx) {
    sx = ParseFloat(attr_sx);
  }

  const char *attr_sy = node.GetAttr("scaleY");
  if (attr_sy && *attr_sy) {
    sy = ParseFloat(attr_sy);
  }

  anim->SetScaleDelta(sx, sy);

  ApplyAnimationOriginChildren(anim, node);
}

void LoadPathXML(PathAnim *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  if (anim == nullptr) {
    return;
  }

  const char *curve = node.GetAttr("curve");
  if (curve && *curve) {
    int curve_type = 0;
    if (ParseCurveTypeString(curve, &curve_type)) {
      anim->SetCurve(static_cast<uint8_t>(curve_type));
    }
  }

  for (const auto &child : node.children) {
    if (!IcaseEq(child.tag.c_str(), "ControlPoints")) {
      continue;
    }

    for (const auto &cp_node : child.children) {
      if (!IcaseEq(cp_node.tag.c_str(), "ControlPoint")) {
        continue;
      }

      const char *name = cp_node.GetAttr("name");
      auto *point = anim->CreateControlPoint(name ? name : "");
      if (point == nullptr) {
        continue;
      }

      float offset_x = 0.0f;
      float offset_y = 0.0f;
      if (const char *ox = cp_node.GetAttr("offsetX"); ox && *ox) {
        offset_x = PixelAnimationOffsetToStored(ParseFloat(ox));
      }
      if (const char *oy = cp_node.GetAttr("offsetY"); oy && *oy) {
        offset_y = PixelAnimationOffsetToStored(ParseFloat(oy));
      }

      point->SetOffset(offset_x, offset_y);
    }

    break;
  }
}

void LoadAlphaXML(AlphaAnim *anim, const XmlNode &node) {
  LoadBaseAnimXML(anim, node);

  if (anim == nullptr) {
    return;
  }

  const auto parsed_change =
      ParseAlphaXmlChangeAttribute(node.GetAttr("change"), anim->GetName());
  if (parsed_change.has_value) {
    anim->SetChange(parsed_change.value);
  }

  for (const auto &child : node.children) {
    if (IcaseEq(child.tag.c_str(), "Scripts")) {

    }
  }
}

void LoadAnimGroupXML(AnimationGroup *group, const XmlNode &node) {

  const char *looping = node.GetAttr("looping");
  if (looping && *looping) {
    if (IcaseEq(looping, "REPEAT"))
      group->SetLooping(AnimLoopType::Repeat);
    else if (IcaseEq(looping, "BOUNCE"))
      group->SetLooping(AnimLoopType::Bounce);
    else
      group->SetLooping(AnimLoopType::None);
  }

  float iox = 0.0f, ioy = 0.0f;
  const char *iox_s = node.GetAttr("initialOffsetX");
  if (iox_s && *iox_s)
    iox = ParseFloat(iox_s);
  const char *ioy_s = node.GetAttr("initialOffsetY");
  if (ioy_s && *ioy_s)
    ioy = ParseFloat(ioy_s);
  group->SetInitialOffsetPixels(iox, ioy);

  for (const auto &child : node.children) {
    const char *tag = child.tag.c_str();
    const char *name = child.GetAttr("name");

    if (IcaseEq(tag, "Animation")) {
      LoadCreatedAnimationXML(group->CreateBasicAnimation(name ? name : ""), child,
                              LoadAnimationXML);
    } else if (IcaseEq(tag, "Translation")) {
      LoadCreatedAnimationXML(group->CreateTranslation(name ? name : ""), child,
                              LoadTranslationXML);
    } else if (IcaseEq(tag, "Rotation")) {
      LoadCreatedAnimationXML(group->CreateRotation(name ? name : ""), child, LoadRotationXML);
    } else if (IcaseEq(tag, "Scale")) {
      LoadCreatedAnimationXML(group->CreateScale(name ? name : ""), child, LoadScaleXML);
    } else if (IcaseEq(tag, "Path")) {
      LoadCreatedAnimationXML(group->CreatePath(name ? name : ""), child, LoadPathXML);
    } else if (IcaseEq(tag, "Alpha")) {
      LoadCreatedAnimationXML(group->CreateAlpha(name ? name : ""), child, LoadAlphaXML);
    } else if (IcaseEq(tag, "Scripts")) {

    }

  }
}

}

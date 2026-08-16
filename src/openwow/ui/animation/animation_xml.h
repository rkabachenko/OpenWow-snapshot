
#pragma once

#include "openwow/ui/animation/animation_types.h"
#include "openwow/ui/frame_script_type_info.h"

#include <optional>
#include <string>
#include <vector>

namespace openwow::ui::anim {

class Animation;
class AnimationGroup;
class AlphaAnim;
class TranslationAnim;
class RotationAnim;
class ScaleAnim;
class PathAnim;

int NormalizeXmlAnimationOrder(int xml_order);
bool TryParseAnimationSmoothingString(const char* value,
                                      float* smooth_in,
                                      float* smooth_out,
                                      AnimCurveType* curve = nullptr);
void ApplyXmlAnimationBaseAttributes(Animation* anim,
                                     std::optional<float> duration,
                                     std::optional<float> start_delay,
                                     std::optional<float> end_delay,
                                     std::optional<int> xml_order,
                                     const char* smoothing,
                                     std::optional<float> max_framerate);

using ScriptSlot = openwow::ui::FrameScriptTypeInfo;

const ScriptSlot* GetAnimGroupScriptSlot(const char* handler_name);
const char* NormalizeAnimGroupScriptHandler(const char* handler_name);

const ScriptSlot* GetAnimScriptSlot(const char* handler_name);
const char* NormalizeAnimScriptHandler(const char* handler_name);

struct XmlAttr {
  std::string name;
  std::string value;
};

struct XmlNode {
  std::string tag;
  std::string text;
  std::vector<XmlAttr> attrs;
  std::vector<XmlNode> children;

  const char* GetAttr(const char* name) const {
    for (const auto& a : attrs) {
      if (a.name == name) return a.value.c_str();
    }
    return nullptr;
  }
};

void LoadAnimGroupXML(AnimationGroup* group, const XmlNode& node);

void LoadTranslationXML(TranslationAnim* anim, const XmlNode& node);

void LoadRotationXML(RotationAnim* anim, const XmlNode& node);

void LoadScaleXML(ScaleAnim* anim, const XmlNode& node);

void LoadPathXML(PathAnim* anim, const XmlNode& node);

void LoadAlphaXML(AlphaAnim* anim, const XmlNode& node);

void FinalizeLoadedAnimation(Animation* anim);

}

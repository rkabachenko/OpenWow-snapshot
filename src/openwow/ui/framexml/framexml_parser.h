#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::xml {
struct XMLNode;
}

namespace openwow::ui::framexml {

struct TopLevelFrameGroup {
  std::size_t first_frame{0};
  std::size_t frame_count{0};
};

struct ParseResult {
  bool ok{false};
  std::string error;
  std::vector<UiFrame> frames;
  std::vector<std::string> diagnostics;
  std::vector<TopLevelFrameGroup> top_level_groups;
};

ParseResult ParseFrameXml(const std::string& xml_text);

ParseResult ParseFrameXml(const openwow::ui::xml::XMLNode& root);

void RegisterVirtualTemplate(const UiFrame& frame);
using VirtualTemplateRegistrySnapshot = std::unordered_map<std::string, UiFrame>;
[[nodiscard]] VirtualTemplateRegistrySnapshot CaptureVirtualTemplates();
void RestoreVirtualTemplates(VirtualTemplateRegistrySnapshot snapshot);
const UiFrame* GetVirtualTemplate(const std::string& name);
const UiAnimationGroup* GetVirtualAnimationGroupTemplate(const std::string& name);
const UiAnimation* GetVirtualAnimationTemplate(const std::string& name);
const UiPathControlPoint* GetVirtualControlPointTemplate(const std::string& name);
void ClearVirtualTemplates();

}

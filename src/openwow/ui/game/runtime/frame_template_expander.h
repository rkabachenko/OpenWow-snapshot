#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::ui::game::runtime {

enum class FrameTemplateValidation {
  Found,
  Missing,
  Recursive,
};

struct ExpandedFramePlan {
  std::vector<openwow::ui::framexml::UiFrame> frames;
  std::vector<std::vector<std::size_t>> children;
  std::vector<std::size_t> parents;
  std::size_t source_nodes{0};
  std::size_t inherited_nodes{0};
};

FrameTemplateValidation ValidateFrameTemplateChain(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &template_name);
std::optional<std::string> ResolveInheritedParentName(
    const std::unordered_map<std::string, std::vector<openwow::ui::framexml::UiFrame>> &templates,
    const std::string &inherits);
void CollectFrameTemplates(const std::vector<openwow::ui::framexml::UiFrame> &parsed_frames,
                           std::unordered_map<std::string,
                                              std::vector<openwow::ui::framexml::UiFrame>>
                               &out_templates);
void MergeInheritedFrameDefinition(openwow::ui::framexml::UiFrame &destination,
                                   const openwow::ui::framexml::UiFrame &source);

ExpandedFramePlan BuildExpandedFramePlan(openwow::ui::framexml::UiFrame root_frame,
                                         std::vector<openwow::ui::framexml::UiFrame> local_children,
                                         const std::unordered_map<
                                             std::string,
                                             std::vector<openwow::ui::framexml::UiFrame>> &templates,
                                         const std::string &root_parent_lua_scope);

}

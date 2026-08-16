#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

class GlueCharSelectScene;
class GlueWidgetRuntime;
struct GlueWidgetState;

[[nodiscard]] std::vector<std::string> CollectGlueStreamingModelPaths(
    const GlueWidgetRuntime& widgets,
    const GlueCharSelectScene* attached_scene,
    std::string_view attached_scene_host_widget_name);

[[nodiscard]] std::vector<std::string> CollectGlueStreamingModelPaths(
    const std::vector<GlueWidgetState>& visible_widgets,
    const GlueCharSelectScene* attached_scene,
    std::string_view attached_scene_host_widget_name);

}

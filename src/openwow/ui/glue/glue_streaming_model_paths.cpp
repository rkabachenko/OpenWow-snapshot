#include "openwow/ui/glue/glue_streaming_model_paths.h"

#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::glue {

namespace {

void AppendUniquePath(std::vector<std::string>& paths, const std::string& path) {
  if (path.empty()) {
    return;
  }
  for (const auto& existing : paths) {
    if (openwow::text::EqualsIgnoreCaseAscii(existing, path)) {
      return;
    }
  }
  paths.push_back(path);
}

bool IsModelWidgetKind(const std::string& kind) {
  const auto lower = openwow::text::ToLowerAscii(kind);
  return lower == "model" || lower == "modelffx";
}

}

std::vector<std::string> CollectGlueStreamingModelPaths(
    const GlueWidgetRuntime& widgets,
    const GlueCharSelectScene* attached_scene,
    const std::string_view attached_scene_host_widget_name) {
  return CollectGlueStreamingModelPaths(widgets.VisibleWidgetsInRenderOrder(),
                                        attached_scene,
                                        attached_scene_host_widget_name);
}

std::vector<std::string> CollectGlueStreamingModelPaths(
    const std::vector<GlueWidgetState>& visible_widgets,
    const GlueCharSelectScene* attached_scene,
    const std::string_view attached_scene_host_widget_name) {
  std::vector<std::string> paths;
  bool attached_scene_host_visible = false;

  for (const auto& widget : visible_widgets) {
    if (!IsModelWidgetKind(widget.kind) || widget.model_file.empty()) {
      continue;
    }

    AppendUniquePath(paths, widget.model_file);
    if (!attached_scene_host_widget_name.empty() && widget.name == attached_scene_host_widget_name) {
      attached_scene_host_visible = true;
    }
  }

  if (attached_scene == nullptr || !attached_scene_host_visible) {
    return paths;
  }

  AppendUniquePath(paths, attached_scene->selected_character_model_path());
  if (const auto& prop_path = attached_scene->prop_model_path(); prop_path.has_value()) {
    AppendUniquePath(paths, *prop_path);
  }
  for (const auto& effect : attached_scene->character_effect_models()) {
    if (effect.active) {
      AppendUniquePath(paths, effect.model_path);
    }
  }
  for (const auto& equipment : attached_scene->character_equipment_models()) {
    if (equipment.active) {
      AppendUniquePath(paths, equipment.model_path);
      for (const auto& child : equipment.child_models) {
        if (child.active) {
          AppendUniquePath(paths, child.model_path);
        }
      }
    }
  }

  return paths;
}

}

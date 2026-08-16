#pragma once

#include "openwow/render/api/render_scene.h"

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::client {

[[nodiscard]] openwow::render::api::RendererCreateInfo::PresentationConfig
BuildPresentationConfig(const openwow::ui::game::CVarSystem& cvars);

}

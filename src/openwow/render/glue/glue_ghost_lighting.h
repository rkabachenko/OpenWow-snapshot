#pragma once

#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/render/models/animation/model_render_callback_pipeline.h"

#include <cstdint>
#include <optional>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::render::glue {

inline constexpr std::uint32_t kCharacterSelectGhostLightParamsId = 3u;
inline constexpr std::uint32_t kCharacterSelectGhostLightTimeTick = 0u;

[[nodiscard]] ModelRenderCallbackGhostLightSample SampleCharacterSelectGhostLight(
    const data::dbc::LightIntBandEntry& diffuse_band,
    const data::dbc::LightIntBandEntry& ambient_band,
    std::uint32_t time_tick = kCharacterSelectGhostLightTimeTick);

[[nodiscard]] std::optional<ModelRenderCallbackGhostLightSample>
ResolveCharacterSelectGhostLight(const data::dbc::DbcLoader* dbc_loader);

}

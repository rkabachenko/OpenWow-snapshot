#pragma once

#include "openwow/data/wmo/wmo_file.h"
#include "openwow/world/coordinates/world_geometry.h"
#include "openwow/world/wmo/wmo_visibility.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace openwow::world {

struct WmoFogLiquidState {
  std::uint32_t liquid_type_id = 0u;
  std::uint32_t liquid_type_flags = 0u;
};

[[nodiscard]] std::optional<FogState> ResolveRetailWmoFog(
    const data::wmo::WmoRoot& root,
    const data::wmo::WmoGroupHeader& containing_group,
    const Vec3& local_camera_position, float far_clip,
    WmoFogLiquidState liquid) noexcept;

[[nodiscard]] float ResolveWmoFogPortalBlendProgress(
    const WmoVisibilityData& visibility,
    std::span<const std::size_t> containment_group_indices,
    const Vec3& local_camera_position) noexcept;

[[nodiscard]] FogState BlendWmoFogPortalTransition(const FogState& outdoor_fog,
                                                    const FogState& wmo_fog,
                                                    float progress) noexcept;

}

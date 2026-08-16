
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::game {

class WorldSession;

inline constexpr std::uint32_t kTabardCreationCostCopper = 100000;
inline constexpr std::size_t kTabardNumAxes = 5;

inline constexpr std::array<std::uint32_t, kTabardNumAxes>
    kTabardVariationLimits = {
    170,
    17,
    6,
    17,
    51,
};

void TabardFrame_RefreshActivePlayerPreview(WorldSession *session);

bool TabardFrame_CycleVariation(uint32_t* designValues,
                                 uint32_t axisIndex, int32_t delta);

[[nodiscard]] bool TabardFrame_Save(
    WorldSession& session,
    const std::array<std::uint32_t, kTabardNumAxes>& design_values,
    std::uint64_t vendor_guid);

void TabardFrame_HandleSaveResult(WorldSession& session,
                                  std::uint32_t result_code);

[[nodiscard]] bool TabardFrame_InitializeColors(WorldSession* session,
                                                uint32_t* design_values);

void TabardFrame_InitializeRandomDesign(uint32_t* design_values,
                                        uint32_t tick_count32);

}

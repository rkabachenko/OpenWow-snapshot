
#pragma once

#include <cstdint>

namespace openwow::core::ida {

struct FrameTimingCache {
    std::int32_t  cumulative_1024_ticks  = 0;

    std::int32_t  frame_1024_tick_delta  = 0;

    float         cumulative_seconds     = 0.f;

    float         frame_delta_seconds    = 0.f;

    float         elapsed_ms_as_seconds  = 0.f;

    std::int32_t  frame_delta_ms_rounded = 0;

    std::int32_t  elapsed_ms             = 0;

};

[[nodiscard]] const FrameTimingCache& GetFrameTimingCache() noexcept;

[[nodiscard]] std::int32_t FrameTiming_SecondsTo1024Ticks(float delta_seconds) noexcept;

void FrameTiming_UpdateCaches(float delta_seconds, std::int32_t elapsed_ms) noexcept;

}

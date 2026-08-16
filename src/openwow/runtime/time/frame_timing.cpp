
#include "openwow/runtime/time/frame_timing.h"

namespace openwow::core::ida {

static FrameTimingCache s_cache{};

const FrameTimingCache& GetFrameTimingCache() noexcept {
    return s_cache;
}

std::int32_t FrameTiming_SecondsTo1024Ticks(float delta_seconds) noexcept {
    const float scaled = delta_seconds * 1024.0f;
    return static_cast<std::int32_t>(scaled - 0.5f);
}

void FrameTiming_UpdateCaches(float delta_seconds, std::int32_t elapsed_ms) noexcept {
    s_cache.elapsed_ms = elapsed_ms;

    const float ms_f = delta_seconds * 1000.0f;
    s_cache.frame_delta_ms_rounded = static_cast<std::int32_t>(ms_f - 0.5f);

    s_cache.elapsed_ms_as_seconds =
        static_cast<float>(static_cast<double>(
            static_cast<std::uint32_t>(elapsed_ms)) * 0.001);

    s_cache.frame_delta_seconds = delta_seconds;
    s_cache.cumulative_seconds += delta_seconds;

    const std::int32_t ticks_1024 = FrameTiming_SecondsTo1024Ticks(delta_seconds);
    s_cache.cumulative_1024_ticks += ticks_1024;
    s_cache.frame_1024_tick_delta = ticks_1024;
}

}

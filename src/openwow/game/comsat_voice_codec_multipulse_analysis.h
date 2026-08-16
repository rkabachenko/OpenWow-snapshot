
#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kMultiPulseWindowSize = 40;

inline constexpr std::size_t kMultiPulseSubframeStride = 211;

inline constexpr std::size_t kFilterStateFloatOffset = 131;

inline constexpr std::int16_t kGainQuantLevels = 32;

inline constexpr float kGainQuantLowerBound = 0.699999988079071f;

inline constexpr float kGainQuantUpperBound = 10.18f;

inline constexpr double kGainQuantStep = 0.3058064579963684;

inline constexpr std::int16_t kUsageFlagResetInterval = 6;

inline constexpr std::size_t kUsageFlagCount = 20;

void ComSatVoiceCodec_MultiPulseSubframeAnalysis(
    float* subframe_state,
    float* signal_buffer,
    const float* target_signal,
    const float* correlation_matrix,
    const float* autocorrelation,
    std::uint16_t* usage_flags,
    std::int16_t is_last_subframe) noexcept;

void ComSatVoiceCodec_MultiPulseFrameAnalysis(
    float* frame_state,
    std::int16_t subframe_count,
    float* target_signal,
    std::int16_t last_subframe_flag,
    float* signal_buffer) noexcept;

}

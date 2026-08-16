
#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kComSatResamplerBlockSamples = 10;

inline constexpr std::size_t kComSatResamplerBlockCount = 8;

struct ComSatVoiceCodecResamplerState {

    std::array<float, kComSatResamplerBlockCount * kComSatResamplerBlockSamples>
        sample_blocks{};

    std::array<float, kComSatResamplerBlockCount> frame_weights{};

    std::uint16_t write_position{0};

    std::uint16_t reserved_a{0};

    std::uint16_t reserved_b{0};

    std::uint16_t pending_rate_ratio{1};
};

static_assert(sizeof(ComSatVoiceCodecResamplerState) == 360,
              "Resampler state must be 360 bytes to match IDA layout");

void ComSatVoiceCodec_ResetResamplerState(
    ComSatVoiceCodecResamplerState& state) noexcept;

}

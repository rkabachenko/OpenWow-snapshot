
#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kComSatLspOrder = 10;

inline constexpr std::size_t kComSatGainHistorySize = 5;

inline constexpr std::uint32_t kComSatLspDefaultRandomSeed = 0x1234u;

inline constexpr std::uint16_t kComSatLspDefaultFrameAccumulator = 40u;

inline constexpr std::array<float, kComSatLspOrder> kComSatDefaultLspCosines{{
    0.95949297f,
    0.84125352f,
    0.65486073f,
    0.41541502f,
    0.14231484f,
    -0.14231484f,
    -0.41541502f,
    -0.65486073f,
    -0.84125352f,
    -0.95949297f,
}};

struct ComSatVoiceCodecLspPredictorState {
    std::array<float, kComSatLspOrder> lsp_cosines{};

    std::array<float, kComSatGainHistorySize> primary_gain_history{};
    float primary_gain_cap{0.0f};

    std::array<float, kComSatGainHistorySize> secondary_gain_history{};
    float secondary_gain_cap{0.0f};

    std::uint16_t frame_accumulator{0};
    std::uint16_t follow_phase{0};
    std::uint16_t reserved_a{0};
    std::uint16_t reserved_b{0};
    std::uint32_t random_seed{0};
};

static_assert(sizeof(ComSatVoiceCodecLspPredictorState) == 100,
              "LSP predictor state must be 100 bytes to match IDA layout");

void ComSatVoiceCodec_InitLspPredictorState(
    ComSatVoiceCodecLspPredictorState& state,
    const float* source_data) noexcept;

}

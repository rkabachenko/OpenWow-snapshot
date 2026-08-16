
#pragma once

#include <array>
#include <cstdint>

namespace openwow::game {

inline constexpr std::size_t kLspOrder = 10;

inline constexpr std::array<std::uint32_t, 4> kAbsoluteCodebookSizes{{128, 128, 128, 64}};

inline constexpr std::array<std::uint32_t, 3> kDifferentialCodebookSizes{{128, 128, 64}};

inline constexpr float kLspSmoothOld = 0.9f;

inline constexpr float kLspSmoothNew = 0.1f;

inline constexpr std::size_t kCodebookAbsoluteOffset = 0;

inline constexpr std::size_t kCodebookDiffAOffset = 4480;

inline constexpr std::size_t kCodebookDiffBOffset = 7680;

inline constexpr std::size_t kMeanLspOffset = 11760;

inline constexpr std::size_t kDiffWeightsAOffset = 11772;

inline constexpr std::size_t kDiffWeightsBOffset = 11784;

inline constexpr std::size_t kPreviousLspOffset = 11796;

int ComSatVoiceCodec_UnquantizeLspNarrowband(
    float* scratch_cosines,
    const std::uint32_t* codebook_indices,
    std::int16_t is_interpolating,
    float* lsp_output,
    const float* prev_cosines,
    const float* codec_params) noexcept;

void ComSatVoiceCodec_CosinesToLspFrequencies(
    const float* cosines,
    float* lsp_out) noexcept;

int ComSatVoiceCodec_StabilizeLsp(float* lsp) noexcept;

}

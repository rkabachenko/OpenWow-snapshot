
#include "openwow/game/comsat_voice_codec_lsp_unquantize.h"

#include <cmath>
#include <cstring>

namespace openwow::game {

namespace {

constexpr float kPi = 3.14159274101257324f;

constexpr float kMinLspGap = 0.006250000093132257f;
constexpr float kMinFinalGap = 0.006187500092200935f;

inline void CopyFloat10(float* dst, const float* src) noexcept {
    std::memcpy(dst, src, kLspOrder * sizeof(float));
}

inline void AddCodebookVector(float* lsp, const float* codebook,
                              std::uint32_t index) noexcept {
    const float* vec = codebook + index * kLspOrder;
    for (std::size_t i = 0; i < kLspOrder; ++i)
        lsp[i] += vec[i];
}

}

void ComSatVoiceCodec_CosinesToLspFrequencies(
    const float* cosines,
    float* lsp_out) noexcept {

    for (std::size_t i = 0; i < kLspOrder; ++i) {
        float c = cosines[i];

        float angle = std::atan(std::sqrt((1.0f - c) / (c + 1.0f)));
        lsp_out[i] = (angle + angle) / kPi;
    }
}

int ComSatVoiceCodec_StabilizeLsp(float* lsp) noexcept {

    for (int pass = 0; pass < 10; ++pass) {
        bool swapped = false;
        for (int j = 0; j < 9; ++j) {
            if (lsp[j + 1] < lsp[j]) {
                swapped = true;
                float tmp = lsp[j + 1];
                lsp[j + 1] = lsp[j];
                lsp[j] = tmp;
            }
        }
        if (!swapped)
            break;
    }

    int remaining = 10;
    do {
        for (int i = 1; i < 10; ++i) {
            float gap = lsp[i] - lsp[i - 1];
            if (gap < kMinLspGap) {
                float push_right = (kMinLspGap - gap) * 0.5f;
                float push_left = push_right;

                if (i == 1) {
                    if (lsp[0] < kMinLspGap)
                        push_left = lsp[0] * 0.5f;
                } else {
                    float left_gap = lsp[i - 1] - lsp[i - 2];
                    if (left_gap < kMinLspGap)
                        push_left = 0.0f;
                    else if (left_gap < 2.0f * kMinLspGap)
                        push_left = (left_gap - kMinLspGap) * 0.5f;
                }

                if (i == 9) {
                    if (lsp[9] > (1.0f - kMinLspGap))
                        push_right = (1.0f - lsp[9]) * 0.5f;
                } else {
                    float right_gap = lsp[i + 1] - lsp[i];
                    if (right_gap < kMinLspGap)
                        push_right = 0.0f;
                    else if (right_gap < 2.0f * kMinLspGap)
                        push_right = (right_gap - kMinLspGap) * 0.5f;
                }

                lsp[i - 1] -= push_left;
                lsp[i] += push_right;
            }
        }
        --remaining;
    } while (remaining);

    for (int i = 1; i < 10; ++i) {
        if (lsp[i] - lsp[i - 1] < kMinFinalGap)
            return 0;
    }
    return 1;
}

int ComSatVoiceCodec_UnquantizeLspNarrowband(
    float* scratch_cosines,
    const std::uint32_t* codebook_indices,
    std::int16_t is_interpolating,
    float* lsp_output,
    const float* prev_cosines,
    const float* codec_params) noexcept {

    if (is_interpolating) {

        const float* mean_lsp = codec_params + kMeanLspOffset;
        for (std::size_t i = 0; i < kLspOrder; ++i) {
            lsp_output[i] = kLspSmoothOld * lsp_output[i]
                          + kLspSmoothNew * mean_lsp[i];
        }

    } else if (prev_cosines) {

        CopyFloat10(lsp_output, codec_params + kPreviousLspOffset);

        ComSatVoiceCodec_CosinesToLspFrequencies(prev_cosines, scratch_cosines);

        const float* codebook_base;
        const float* delta_weights;
        if (codebook_indices[3]) {
            codebook_base = codec_params + kCodebookDiffAOffset;
            delta_weights = codec_params + kDiffWeightsAOffset;
        } else {
            codebook_base = codec_params + kCodebookDiffBOffset;
            delta_weights = codec_params + kDiffWeightsBOffset;
        }

        const float* prev_lsp = codec_params + kPreviousLspOffset;
        for (std::size_t i = 0; i < kLspOrder; ++i) {
            lsp_output[i] += (scratch_cosines[i] - prev_lsp[i]) * delta_weights[i];
        }

        const float* sub_codebook = codebook_base;
        for (std::size_t s = 0; s < kDifferentialCodebookSizes.size(); ++s) {
            AddCodebookVector(lsp_output, sub_codebook, codebook_indices[s]);
            sub_codebook += kDifferentialCodebookSizes[s] * kLspOrder;
        }

    } else {

        CopyFloat10(lsp_output, codec_params + kMeanLspOffset);

        const float* sub_codebook = codec_params + kCodebookAbsoluteOffset;
        for (std::size_t s = 0; s < kAbsoluteCodebookSizes.size(); ++s) {
            AddCodebookVector(lsp_output, sub_codebook, codebook_indices[s]);
            sub_codebook += kAbsoluteCodebookSizes[s] * kLspOrder;
        }
    }

    int result = ComSatVoiceCodec_StabilizeLsp(lsp_output);

    for (std::size_t i = 0; i < kLspOrder; ++i) {
        scratch_cosines[i] = std::cos(lsp_output[i] * kPi);
    }

    return result;
}

}

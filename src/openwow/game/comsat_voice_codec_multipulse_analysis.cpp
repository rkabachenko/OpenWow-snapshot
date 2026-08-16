
#include "openwow/game/comsat_voice_codec_multipulse_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace openwow::game {

namespace {

float DotProduct(const float* a, const float* b, int count) noexcept {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
        sum += a[i] * b[i];
    return sum;
}

void CopyFloat40(float* dst, const float* src) noexcept {
    std::memcpy(dst, src, kMultiPulseWindowSize * sizeof(float));
}

void ZeroFloat40(float* dst) noexcept {
    std::memset(dst, 0, kMultiPulseWindowSize * sizeof(float));
}

void ScaleFloat40(float* data, float scalar) noexcept {
    for (std::size_t i = 0; i < kMultiPulseWindowSize; ++i)
        data[i] *= scalar;
}

void SubtractFloat40(float* a, const float* b) noexcept {
    for (std::size_t i = 0; i < kMultiPulseWindowSize; ++i)
        a[i] -= b[i];
}

void QuantizeScalar(std::int16_t levels,
                    float* value,
                    std::int16_t* index,
                    float lower,
                    float upper) noexcept {
    const double step =
        (static_cast<double>(upper) - static_cast<double>(lower)) /
        static_cast<double>(levels - 1);
    std::int16_t bin = 0;
    double threshold = 0.5 * step + static_cast<double>(lower);

    for (std::int16_t i = 0; i < static_cast<std::int16_t>(levels - 1); ++i) {
        if (threshold > static_cast<double>(*value))
            break;
        ++bin;
        threshold += step;
    }

    *value = static_cast<float>(lower + step * static_cast<double>(bin));
    *index = bin;
}

inline std::int16_t GetPulseOrder(const float* state) noexcept {
    return *reinterpret_cast<const std::int16_t*>(state);
}

inline float& PulseAmplitude(float* state, int k) noexcept {
    return state[k + 1];
}

inline std::int16_t& PulsePosition(float* state, int k) noexcept {
    return *(reinterpret_cast<std::int16_t*>(state) + 12 + k);
}

inline std::int16_t& GainQuantIndex(float* state) noexcept {
    return *(reinterpret_cast<std::int16_t*>(state) + 20);
}

}

void ComSatVoiceCodec_MultiPulseSubframeAnalysis(
    float* subframe_state,
    float* signal_buffer,
    const float* target_signal,
    const float* correlation_matrix,
    const float* autocorrelation,
    std::uint16_t* usage_flags,
    std::int16_t is_last_subframe) noexcept {

    const std::int16_t order = GetPulseOrder(subframe_state);

    float work_corr[kMultiPulseWindowSize];
    CopyFloat40(work_corr, correlation_matrix);

    float best_abs = 0.0f;
    for (int i = 0; i < static_cast<int>(kMultiPulseWindowSize); ++i) {
        if (usage_flags[i] != 0)
            continue;
        const float abs_val = std::fabs(work_corr[i]);
        if (best_abs <= abs_val) {
            best_abs = abs_val;
            PulsePosition(subframe_state, 0) = static_cast<std::int16_t>(i);
        }
    }

    float amplitude = std::fabs(best_abs / autocorrelation[0]);
    if (work_corr[PulsePosition(subframe_state, 0)] < 0.0f)
        amplitude = -amplitude;
    PulseAmplitude(subframe_state, 0) = amplitude;
    usage_flags[PulsePosition(subframe_state, 0)] = 1;

    int pulse_idx = 1;
    if (order > 1) {
        while (true) {
            best_abs = -3.4028235e38f;

            const std::int16_t prev_pos =
                PulsePosition(subframe_state, pulse_idx - 1);
            const float prev_amp =
                PulseAmplitude(subframe_state, pulse_idx - 1);

            for (int j = 0; j < static_cast<int>(kMultiPulseWindowSize); ++j) {
                if (usage_flags[j] != 0)
                    continue;

                const int diff = j - static_cast<int>(prev_pos);
                const int abs_diff = std::abs(diff);
                work_corr[j] -= autocorrelation[abs_diff] * prev_amp;

                const float abs_val = std::fabs(work_corr[j]);
                if (best_abs < abs_val) {
                    best_abs = abs_val;
                    PulsePosition(subframe_state, pulse_idx) =
                        static_cast<std::int16_t>(j);
                }
            }

            const std::int16_t cur_pos =
                PulsePosition(subframe_state, pulse_idx);
            float amp = std::fabs(best_abs / autocorrelation[0]);
            if (0.0f >= work_corr[cur_pos])
                amp = -amp;
            PulseAmplitude(subframe_state, pulse_idx) = amp;
            usage_flags[cur_pos] = 1;

            if (++pulse_idx >= order)
                break;
        }
    }

    float excitation_filter[kMultiPulseWindowSize];
    ZeroFloat40(excitation_filter);

    for (int k = 0; k < order; ++k) {
        const std::int16_t pos = PulsePosition(subframe_state, k);
        const float amp = PulseAmplitude(subframe_state, k);
        const float* src = target_signal;

        if (amp <= 0.0f) {
            for (int i = pos; i < static_cast<int>(kMultiPulseWindowSize); ++i)
                excitation_filter[i] = excitation_filter[i] - *src++;
        } else {
            for (int i = pos; i < static_cast<int>(kMultiPulseWindowSize); ++i)
                excitation_filter[i] = *src++ + excitation_filter[i];
        }
    }

    const float numerator = DotProduct(signal_buffer, excitation_filter,
                                       static_cast<int>(kMultiPulseWindowSize));
    const float denominator =
        DotProduct(excitation_filter, excitation_filter,
                   static_cast<int>(kMultiPulseWindowSize));

    float log_gain = std::fabs(numerator / denominator);
    log_gain = std::log(log_gain);

    std::int16_t quant_index = 0;
    QuantizeScalar(kGainQuantLevels, &log_gain, &quant_index,
                   kGainQuantLowerBound, kGainQuantUpperBound);
    GainQuantIndex(subframe_state) = quant_index;

    const float reconstructed_log =
        static_cast<float>(static_cast<double>(quant_index) * kGainQuantStep +
                           static_cast<double>(kGainQuantLowerBound));
    float gain = std::exp(reconstructed_log);

    for (int k = 0; k < order; ++k) {
        if (0.0f >= PulseAmplitude(subframe_state, k))
            PulseAmplitude(subframe_state, k) = -gain;
        else
            PulseAmplitude(subframe_state, k) = gain;
    }

    ScaleFloat40(excitation_filter, gain);

    SubtractFloat40(signal_buffer, excitation_filter);

    if (!is_last_subframe) {
        float* matrix_ptr = const_cast<float*>(correlation_matrix);
        for (int m = 0; m < static_cast<int>(kMultiPulseWindowSize); ++m) {
            for (int k = 0; k < order; ++k) {
                const int diff =
                    m - static_cast<int>(PulsePosition(subframe_state, k));
                const int abs_diff = std::abs(diff);
                matrix_ptr[m] -= autocorrelation[abs_diff] *
                                 PulseAmplitude(subframe_state, k);
            }
        }
    }

    CopyFloat40(subframe_state + kFilterStateFloatOffset, excitation_filter);
}

void ComSatVoiceCodec_MultiPulseFrameAnalysis(
    float* frame_state,
    std::int16_t subframe_count,
    float* target_signal,
    std::int16_t last_subframe_flag,
    float* signal_buffer) noexcept {

    std::uint16_t usage_flags[kUsageFlagCount] = {};

    float corr_matrix[2 * kMultiPulseWindowSize];
    for (int m = 0; m < static_cast<int>(kMultiPulseWindowSize); ++m) {
        const int len = static_cast<int>(kMultiPulseWindowSize) - m;
        corr_matrix[m] = DotProduct(signal_buffer + m, target_signal, len);
    }

    float autocorr[kMultiPulseWindowSize + 2];
    for (int k = 0; k < static_cast<int>(kMultiPulseWindowSize); ++k) {
        const int len = static_cast<int>(kMultiPulseWindowSize) - k;
        const float r = DotProduct(target_signal, target_signal + k, len);
        autocorr[k] = r;

        corr_matrix[2 * kMultiPulseWindowSize - 1 - k] = r;
    }

    ComSatVoiceCodec_MultiPulseSubframeAnalysis(
        frame_state, signal_buffer, target_signal,
        corr_matrix, autocorr, usage_flags, last_subframe_flag);

    if (subframe_count > 1) {
        float* subframe = frame_state + kMultiPulseSubframeStride;

        for (std::int16_t sf = 1; sf < subframe_count; ++sf) {

            if (sf >= kUsageFlagResetInterval) {
                std::memset(usage_flags, 0, sizeof(usage_flags));
            }

            if (last_subframe_flag) {
                SubtractFloat40(signal_buffer,
                                reinterpret_cast<const float*>(subframe + 51));

                for (int m = 0;
                     m < static_cast<int>(kMultiPulseWindowSize); ++m) {
                    const int len =
                        static_cast<int>(kMultiPulseWindowSize) - m;
                    corr_matrix[m] =
                        DotProduct(target_signal,
                                   signal_buffer + m, len);
                }
            }

            ComSatVoiceCodec_MultiPulseSubframeAnalysis(
                subframe, signal_buffer, target_signal,
                corr_matrix, autocorr, usage_flags, last_subframe_flag);

            subframe += kMultiPulseSubframeStride;
        }
    }
}

}

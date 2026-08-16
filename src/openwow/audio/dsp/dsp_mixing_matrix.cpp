
#include "openwow/audio/dsp/dsp_mixing_matrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::audio {

void DspMixingMatrix::Init(std::int16_t num_channels,
                           std::int16_t num_coeffs_per_channel,
                           float scale) {
  num_channels_ = num_channels;
  num_coeffs_per_channel_ = num_coeffs_per_channel;
  scale_ = scale;
  interpolation_counter_ = 0;
  dirty_ = false;

  const auto ch = static_cast<std::size_t>(num_channels);
  const auto n = static_cast<std::size_t>(num_coeffs_per_channel);

  target_buffers_.assign(ch, std::vector<float>(n, 0.0f));
  current_buffers_.assign(ch, std::vector<float>(n, 0.0f));
  delta_buffers_.assign(ch, std::vector<float>(n, 0.0f));
}

int DspMixingMatrix::SetTargetCoefficients(const float* data, int stride) {
  if (!data) {
    return 37;
  }

  if (stride == 0) {
    return 0;
  }

  const int nc = num_channels_;
  const int ncoeff = num_coeffs_per_channel_;

  if (ncoeff == kFastPathCoeffs && stride < (kFastPathMaxStride + 1)) {
    if (stride == 1) {

      for (int ch = 0; ch < nc; ++ch) {
        target_buffers_[ch][0] = data[ch];
      }
    } else {

      for (int ch = 0; ch < nc; ++ch) {
        target_buffers_[ch][0] = data[ch * 2];
        target_buffers_[ch][1] = data[ch * 2 + 1];
      }
    }
  } else {
    int flat_offset = 0;
    for (int ch = 0; ch < nc; ++ch) {
      for (int c = 0; c < ncoeff; ++c) {
        if (c >= stride) {
          target_buffers_[ch][c] = 0.0f;
        } else {
          target_buffers_[ch][c] = data[flat_offset + c];
        }
      }
      flat_offset += stride;
    }
  }

  dirty_ = true;

  return CalcDeltaCoefficients();
}

int DspMixingMatrix::GetTargetCoefficients(float* out, int stride) const {
  if (!out) {
    return 37;
  }

  for (int ch = 0; ch < num_channels_; ++ch) {
    for (int c = 0; c < stride; ++c) {
      if (c >= num_coeffs_per_channel_) {
        *out = 0.0f;
      } else {
        *out = target_buffers_[ch][c];
      }
      ++out;
    }
  }
  return 0;
}

int DspMixingMatrix::ApplyAndInterpolate(float* output, const float* input,
                                          int num_outputs, int num_inputs,
                                          unsigned int num_frames) {
  if (num_frames == 0) {
    return 0;
  }

  for (unsigned int frame = 0; frame < num_frames; ++frame) {
    if (num_outputs > 0) {
      for (int out_ch = 0; out_ch < num_outputs; ++out_ch) {
        float sum = *output;

        float* current = current_buffers_[out_ch].data();
        const float* delta = delta_buffers_[out_ch].data();

        for (int in_ch = 0; in_ch < num_inputs; ++in_ch) {
          sum += input[in_ch] * current[in_ch];
          current[in_ch] += delta[in_ch];
        }

        *output = sum;
        ++output;
      }
    }
    input += num_inputs;
  }

  interpolation_counter_ -= static_cast<std::int16_t>(num_frames);
  return 0;
}

int DspMixingMatrix::CalcDeltaCoefficients() {
  float abs_delta_sum = 0.0f;
  const int nc = num_channels_;
  const int ncoeff = num_coeffs_per_channel_;

  if (ncoeff == kFastPathCoeffs) {
    for (int ch = 0; ch < nc; ++ch) {
      for (int c = 0; c < kFastPathCoeffs; ++c) {
        const float delta =
            (target_buffers_[ch][c] * scale_ - current_buffers_[ch][c]) *
            kInterpolationFactor;
        abs_delta_sum += std::fabs(delta);
        delta_buffers_[ch][c] = delta;
      }
    }
  } else {
    for (int ch = 0; ch < nc; ++ch) {
      for (int c = 0; c < ncoeff; ++c) {
        const float delta =
            (target_buffers_[ch][c] * scale_ - current_buffers_[ch][c]) *
            kInterpolationFactor;
        abs_delta_sum += std::fabs(delta);
        delta_buffers_[ch][c] = delta;
      }
    }
  }

  if (abs_delta_sum > kDeltaThreshold) {
    interpolation_counter_ = kInterpolationSteps;
  }

  return 0;
}

const float* DspMixingMatrix::target_buffer(int channel) const {
  return target_buffers_[channel].data();
}

const float* DspMixingMatrix::current_buffer(int channel) const {
  return current_buffers_[channel].data();
}

const float* DspMixingMatrix::delta_buffer(int channel) const {
  return delta_buffers_[channel].data();
}

}

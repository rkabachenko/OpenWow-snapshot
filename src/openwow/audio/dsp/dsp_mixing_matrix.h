#pragma once

#include <cstdint>
#include <vector>

namespace openwow::audio {

class DspMixingMatrix {
 public:
  static constexpr int kMaxChannels = 16;
  static constexpr int kInterpolationSteps = 64;
  static constexpr float kInterpolationFactor = 1.0f / kInterpolationSteps;
  static constexpr float kDeltaThreshold = 0.000001f;
  static constexpr int kFastPathCoeffs = 6;
  static constexpr int kFastPathMaxStride = 2;

  DspMixingMatrix() = default;

  void Init(std::int16_t num_channels,
            std::int16_t num_coeffs_per_channel,
            float scale = 1.0f);

  [[nodiscard]] int SetTargetCoefficients(const float* data, int stride);

  [[nodiscard]] int GetTargetCoefficients(float* out, int stride) const;

  [[nodiscard]] int ApplyAndInterpolate(float* output, const float* input,
                                        int num_outputs, int num_inputs,
                                        unsigned int num_frames);

  [[nodiscard]] int CalcDeltaCoefficients();

  [[nodiscard]] std::int16_t num_channels() const { return num_channels_; }
  [[nodiscard]] std::int16_t num_coeffs_per_channel() const {
    return num_coeffs_per_channel_;
  }
  [[nodiscard]] std::int16_t interpolation_counter() const {
    return interpolation_counter_;
  }
  [[nodiscard]] bool is_dirty() const { return dirty_; }
  [[nodiscard]] float scale() const { return scale_; }

  void set_scale(float s) { scale_ = s; }

  [[nodiscard]] const float* target_buffer(int channel) const;
  [[nodiscard]] const float* current_buffer(int channel) const;
  [[nodiscard]] const float* delta_buffer(int channel) const;

 private:
  std::int16_t num_channels_{0};
  std::int16_t num_coeffs_per_channel_{0};
  std::int16_t interpolation_counter_{0};
  bool dirty_{false};
  float scale_{1.0f};

  std::vector<std::vector<float>> target_buffers_;
  std::vector<std::vector<float>> current_buffers_;
  std::vector<std::vector<float>> delta_buffers_;
};

}

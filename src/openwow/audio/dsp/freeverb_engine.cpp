#include "openwow/audio/dsp/freeverb_engine.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openwow::audio {
namespace {

constexpr float kRoomScaleOffset = 0.7f;
constexpr float kRoomScale = 0.28f;
constexpr float kDampingScale = 0.4f;
constexpr float kWetScale = 3.0f;
constexpr float kDryScale = 2.0f;
constexpr float kDefaultRoomSize = 0.84f;
constexpr float kDefaultDamping = 0.2f;
constexpr float kDefaultWet = 3.0f;
constexpr float kDefaultWidth = 1.0f;
constexpr float kFreezeThreshold = 0.5f;
constexpr float kNormalGain = 0.015f;
constexpr int kStereoSpread = 23;

constexpr std::array<int, 8> kCombSizes = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassSizes = {556, 441, 341, 225};

float FlushDenormal(const float value) noexcept {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  return (bits & 0x7f800000U) == 0U ? 0.0f : value;
}

class CombFilter {
public:
  void Resize(const int size) {
    buffer_.assign(static_cast<std::size_t>(size), 0.0f);
    index_ = 0;
    filter_store_ = 0.0f;
  }

  void Clear() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    index_ = 0;
    filter_store_ = 0.0f;
  }

  void SetFeedback(const float feedback) noexcept {
    feedback_ = feedback;
  }

  void SetDamping(const float damping) noexcept {
    damping_ = damping;
    damping_complement_ = 1.0f - damping;
  }

  [[nodiscard]] float Process(const float input) noexcept {
    float output = FlushDenormal(buffer_[index_]);
    filter_store_ =
        FlushDenormal(damping_complement_ * output + damping_ * filter_store_);
    buffer_[index_] = input + feedback_ * filter_store_;
    index_ = (index_ + 1U) % buffer_.size();
    return output;
  }

private:
  float feedback_ = 0.0f;
  float filter_store_ = 0.0f;
  float damping_ = 0.0f;
  float damping_complement_ = 1.0f;
  std::size_t index_ = 0;
  std::vector<float> buffer_;
};

class AllpassFilter {
public:
  void Resize(const int size) {
    buffer_.assign(static_cast<std::size_t>(size), 0.0f);
    index_ = 0;
  }

  void Clear() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    index_ = 0;
  }

  [[nodiscard]] float Process(const float input) noexcept {
    const float buffered = FlushDenormal(buffer_[index_]);
    buffer_[index_] = input + buffered * 0.5f;
    index_ = (index_ + 1U) % buffer_.size();
    return buffered - input;
  }

private:
  std::size_t index_ = 0;
  std::vector<float> buffer_;
};

struct ReverbParameters {
  float gain = kNormalGain;
  float room_size = kDefaultRoomSize;
  float feedback = kDefaultRoomSize;
  float damping = kDefaultDamping;
  float feedback_damping = kDefaultDamping;
  float wet = kDefaultWet;
  float wet_left = kDefaultWet;
  float wet_right = 0.0f;
  float dry = 0.0f;
  float width = kDefaultWidth;
  float freeze = 0.0f;
};

}

struct FreeverbEngine::State {
  ReverbParameters parameters;
  std::array<CombFilter, 8> comb_left;
  std::array<CombFilter, 8> comb_right;
  std::array<AllpassFilter, 4> allpass_left;
  std::array<AllpassFilter, 4> allpass_right;
  bool initialized = false;

  void UpdateCoefficients() noexcept {
    auto& values = parameters;
    values.wet_left = (values.width + 1.0f) * 0.5f * values.wet;
    values.wet_right = (1.0f - values.width) * 0.5f * values.wet;

    if (values.freeze >= kFreezeThreshold) {
      values.feedback = 1.0f;
      values.feedback_damping = 0.0f;
      values.gain = 0.0f;
    } else {
      values.feedback = values.room_size;
      values.feedback_damping = values.damping;
      values.gain = kNormalGain;
    }

    for (std::size_t index = 0; index < comb_left.size(); ++index) {
      comb_left[index].SetFeedback(values.feedback);
      comb_right[index].SetFeedback(values.feedback);
      comb_left[index].SetDamping(values.feedback_damping);
      comb_right[index].SetDamping(values.feedback_damping);
    }
  }
};

FreeverbEngine::FreeverbEngine() : state_(std::make_unique<State>()) {}

FreeverbEngine::~FreeverbEngine() = default;

void FreeverbEngine::Initialize(const int sample_rate) {
  const float delay_scale =
      static_cast<float>(std::max(sample_rate, 1)) / 44100.0f;
  const auto scaled_delay = [delay_scale](const int samples) {
    return std::max(1, static_cast<int>(std::lround(
                           static_cast<float>(samples) * delay_scale)));
  };
  for (std::size_t index = 0; index < state_->comb_left.size(); ++index) {
    state_->comb_left[index].Resize(scaled_delay(kCombSizes[index]));
    state_->comb_right[index].Resize(
        scaled_delay(kCombSizes[index] + kStereoSpread));
  }
  for (std::size_t index = 0; index < state_->allpass_left.size(); ++index) {
    state_->allpass_left[index].Resize(scaled_delay(kAllpassSizes[index]));
    state_->allpass_right[index].Resize(
        scaled_delay(kAllpassSizes[index] + kStereoSpread));
  }

  state_->parameters = {};
  state_->UpdateCoefficients();
  state_->initialized = true;
}

void FreeverbEngine::Clear() noexcept {
  if (!state_->initialized ||
      state_->parameters.freeze >= kFreezeThreshold) {
    return;
  }
  for (auto& filter : state_->comb_left) {
    filter.Clear();
  }
  for (auto& filter : state_->comb_right) {
    filter.Clear();
  }
  for (auto& filter : state_->allpass_left) {
    filter.Clear();
  }
  for (auto& filter : state_->allpass_right) {
    filter.Clear();
  }
}

void FreeverbEngine::SetRoomSize(const float value) noexcept {
  state_->parameters.room_size = value * kRoomScale + kRoomScaleOffset;
  state_->UpdateCoefficients();
}

void FreeverbEngine::SetDamping(const float value) noexcept {
  state_->parameters.damping = value * kDampingScale;
  state_->UpdateCoefficients();
}

void FreeverbEngine::SetWetLevel(const float value) noexcept {
  state_->parameters.wet = value * kWetScale;
  state_->UpdateCoefficients();
}

void FreeverbEngine::SetDryLevel(const float value) noexcept {
  state_->parameters.dry = value * kDryScale;
}

void FreeverbEngine::SetStereoWidth(const float value) noexcept {
  state_->parameters.width = value;
  state_->UpdateCoefficients();
}

void FreeverbEngine::SetFreezeMode(const bool enabled) noexcept {
  state_->parameters.freeze = enabled ? 1.0f : 0.0f;
  state_->UpdateCoefficients();
}

void FreeverbEngine::Process(const float* input_left,
                             const float* input_right, float* output_left,
                             float* output_right, const int frame_count,
                             const int stride) noexcept {
  if (!state_->initialized || input_left == nullptr || input_right == nullptr ||
      output_left == nullptr || output_right == nullptr || frame_count <= 0 ||
      stride <= 0) {
    return;
  }

  const auto values = state_->parameters;
  for (int frame = 0; frame < frame_count; ++frame) {
    const float input = (*input_left + *input_right) * values.gain;
    float left = 0.0f;
    float right = 0.0f;

    for (std::size_t index = 0; index < state_->comb_left.size(); ++index) {
      left += state_->comb_left[index].Process(input);
      right += state_->comb_right[index].Process(input);
    }
    for (std::size_t index = 0; index < state_->allpass_left.size(); ++index) {
      left = state_->allpass_left[index].Process(left);
      right = state_->allpass_right[index].Process(right);
    }

    *output_left =
        values.wet_left * left + values.wet_right * right +
        values.dry * *input_left;
    *output_right =
        values.wet_left * right + values.wet_right * left +
        values.dry * *input_right;

    input_left += stride;
    input_right += stride;
    output_left += stride;
    output_right += stride;
  }
}

void FreeverbEngine::ProcessInterleaved(const float* input, float* output,
                                        const int frame_count,
                                        const int channel_count) noexcept {
  if (input == nullptr || output == nullptr || frame_count <= 0 ||
      channel_count <= 0) {
    return;
  }

  const auto sample_count =
      static_cast<std::size_t>(frame_count) *
      static_cast<std::size_t>(channel_count);
  if (channel_count < 2) {
    std::copy_n(input, sample_count, output);
    return;
  }
  if (channel_count > 2) {
    std::copy_n(input, sample_count, output);
  }

  Process(input, input + 1, output, output + 1, frame_count, channel_count);
}

}

#pragma once

#include <memory>

namespace openwow::audio {

class FreeverbEngine final {
public:
  FreeverbEngine();
  ~FreeverbEngine();

  FreeverbEngine(const FreeverbEngine&) = delete;
  FreeverbEngine& operator=(const FreeverbEngine&) = delete;
  FreeverbEngine(FreeverbEngine&&) = delete;
  FreeverbEngine& operator=(FreeverbEngine&&) = delete;

  void Initialize(int sample_rate = 44100);
  void Clear() noexcept;

  void SetRoomSize(float value) noexcept;
  void SetDamping(float value) noexcept;
  void SetWetLevel(float value) noexcept;
  void SetDryLevel(float value) noexcept;
  void SetStereoWidth(float value) noexcept;
  void SetFreezeMode(bool enabled) noexcept;

  void Process(const float* input_left, const float* input_right,
               float* output_left, float* output_right, int frame_count,
               int stride) noexcept;
  void ProcessInterleaved(const float* input, float* output, int frame_count,
                          int channel_count) noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

}

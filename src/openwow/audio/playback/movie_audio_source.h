#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::audio {

class IMovieAudioSource {
 public:
  struct Stats {
    std::size_t buffered_samples{0};
    std::size_t capacity_samples{0};
    std::uint64_t consumed_samples{0};
    std::uint64_t underflows{0};
    bool producer_finished{false};
  };

  virtual ~IMovieAudioSource() = default;

  IMovieAudioSource(const IMovieAudioSource&) = delete;
  IMovieAudioSource& operator=(const IMovieAudioSource&) = delete;

  virtual std::size_t MixInto(std::int32_t* mix, std::size_t sample_count,
                              float volume) noexcept = 0;

  [[nodiscard]] virtual bool IsDrained() const noexcept = 0;
  [[nodiscard]] virtual int SampleRate() const noexcept = 0;
  [[nodiscard]] virtual int Channels() const noexcept = 0;
  [[nodiscard]] virtual Stats GetStats() const noexcept = 0;

 protected:
  IMovieAudioSource() = default;
};

}

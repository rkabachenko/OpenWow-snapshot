
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace openwow::game {

class IComSatSoundIOAllocator {
public:
  virtual ~IComSatSoundIOAllocator() = default;

  virtual void *Allocate(std::size_t size) = 0;

  virtual int Free(void *ptr) = 0;
};

class AudioRingBuffer {
public:

  static constexpr std::uint32_t kCapacity = 0x4000;

  static constexpr std::uint32_t kWrapMask = 0x3FFF;

  AudioRingBuffer() = default;

  AudioRingBuffer(const AudioRingBuffer &) = delete;
  AudioRingBuffer &operator=(const AudioRingBuffer &) = delete;

  std::uint32_t Read(float *output, std::uint32_t count);

  bool Write(const float *input, std::uint32_t count);

  [[nodiscard]] std::uint32_t available() const noexcept { return available_count_; }
  [[nodiscard]] std::uint32_t write_position() const noexcept { return write_position_; }

private:
  std::uint32_t write_position_{0};
  std::uint32_t available_count_{0};
  std::mutex mutex_;
  float data_[kCapacity]{};
};

class ComSatSoundIOPerSlotObject {
public:

  explicit ComSatSoundIOPerSlotObject(IComSatSoundIOAllocator *allocator = nullptr);

  virtual ~ComSatSoundIOPerSlotObject();

  ComSatSoundIOPerSlotObject(const ComSatSoundIOPerSlotObject &) = delete;
  ComSatSoundIOPerSlotObject &operator=(const ComSatSoundIOPerSlotObject &) = delete;

  virtual bool WriteData(int offset, std::uint32_t count, float *output);

  int Release();

  [[nodiscard]] IComSatSoundIOAllocator *allocator() const noexcept { return allocator_; }
  [[nodiscard]] bool sound_attached() const noexcept { return sound_attached_; }
  [[nodiscard]] AudioRingBuffer &ring_buffer() noexcept { return ring_buffer_; }
  [[nodiscard]] const AudioRingBuffer &ring_buffer() const noexcept { return ring_buffer_; }

private:
  IComSatSoundIOAllocator *allocator_{nullptr};
  bool sound_attached_{false};
  AudioRingBuffer ring_buffer_;
};

inline double ComSatSoundIOSoundEngine_MaxDbLevel() {
  return 20.0 * std::log10(131.072);
}

struct ComSatSoundIOChannelGroupEntry {

  std::int32_t voice_activity_countdown{0};

  float volume_threshold_db{0.5f};

};

class ComSatSoundIOSoundEngine {
public:
  ComSatSoundIOSoundEngine() = default;
  ~ComSatSoundIOSoundEngine();

  ComSatSoundIOSoundEngine(const ComSatSoundIOSoundEngine &) = delete;
  ComSatSoundIOSoundEngine &operator=(const ComSatSoundIOSoundEngine &) = delete;

  bool Open();

  void ClearAll();

  bool AddChannelGroup(std::uint8_t channel_key, float initial_volume);

  bool RemoveChannelGroup(std::uint8_t channel_key);

  void SetChannelGroupVolume(std::uint8_t channel_key, float volume);

  [[nodiscard]] bool active() const noexcept { return active_; }

  [[nodiscard]] const ComSatSoundIOChannelGroupEntry *
  FindChannelGroup(std::uint8_t channel_key) const;

private:
  bool active_{false};

  std::unordered_map<std::uint8_t, std::unique_ptr<ComSatSoundIOChannelGroupEntry>>
      channel_groups_;
};

}

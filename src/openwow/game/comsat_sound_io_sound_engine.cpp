
#include "openwow/game/comsat_sound_io_sound_engine.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace openwow::game {

std::uint32_t AudioRingBuffer::Read(float *output, std::uint32_t count) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (available_count_ == 0) {
    std::memset(output, 0, count * sizeof(float));
    return 0;
  }

  std::uint32_t read_pos = write_position_;
  if (read_pos >= available_count_) {
    read_pos -= available_count_;
  } else {
    read_pos = read_pos + kCapacity - available_count_;
  }

  const std::uint32_t actual = std::min(count, available_count_);
  available_count_ -= actual;

  for (std::uint32_t i = 0; i < actual; ++i) {
    output[i] = data_[read_pos];
    read_pos = (read_pos + 1) & kWrapMask;
  }

  if (actual < count) {
    std::memset(output + actual, 0, (count - actual) * sizeof(float));
  }

  return actual;
}

bool AudioRingBuffer::Write(const float *input, std::uint32_t count) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (count > kCapacity - available_count_) {
    return false;
  }

  available_count_ += count;

  for (std::uint32_t i = 0; i < count; ++i) {
    data_[write_position_] = input[i];
    write_position_ = (write_position_ + 1) & kWrapMask;
  }

  return true;
}

ComSatSoundIOPerSlotObject::ComSatSoundIOPerSlotObject(IComSatSoundIOAllocator *allocator)
    : allocator_(allocator) {

}

ComSatSoundIOPerSlotObject::~ComSatSoundIOPerSlotObject() {

  sound_attached_ = false;
}

int ComSatSoundIOPerSlotObject::Release() {

  auto *alloc = allocator_;

  this->~ComSatSoundIOPerSlotObject();

  if (alloc) {
    return alloc->Free(this);
  }
  return 0;
}

bool ComSatSoundIOPerSlotObject::WriteData(int , std::uint32_t count,
                                           float *output) {
  return ring_buffer_.Read(output, count) != 0;
}

ComSatSoundIOSoundEngine::~ComSatSoundIOSoundEngine() {
  ClearAll();
}

bool ComSatSoundIOSoundEngine::Open() {
  if (active_) {
    ClearAll();
  }

  active_ = true;
  return true;
}

void ComSatSoundIOSoundEngine::ClearAll() {

  channel_groups_.clear();
  active_ = false;
}

bool ComSatSoundIOSoundEngine::AddChannelGroup(std::uint8_t channel_key,
                                                float initial_volume) {
  if (!active_) {
    return false;
  }

  if (channel_groups_.count(channel_key) != 0) {
    return false;
  }

  auto entry = std::make_unique<ComSatSoundIOChannelGroupEntry>();

  entry->volume_threshold_db =
      static_cast<float>(ComSatSoundIOSoundEngine_MaxDbLevel() * initial_volume);

  channel_groups_.emplace(channel_key, std::move(entry));
  return true;
}

bool ComSatSoundIOSoundEngine::RemoveChannelGroup(std::uint8_t channel_key) {
  if (!active_) {
    return false;
  }

  auto it = channel_groups_.find(channel_key);
  if (it == channel_groups_.end()) {
    return true;

  }

  channel_groups_.erase(it);
  return true;
}

void ComSatSoundIOSoundEngine::SetChannelGroupVolume(std::uint8_t channel_key,
                                                     float volume) {
  auto it = channel_groups_.find(channel_key);
  if (it == channel_groups_.end()) {
    return;
  }

  it->second->volume_threshold_db =
      static_cast<float>(ComSatSoundIOSoundEngine_MaxDbLevel() * volume);
}

const ComSatSoundIOChannelGroupEntry *
ComSatSoundIOSoundEngine::FindChannelGroup(std::uint8_t channel_key) const {
  auto it = channel_groups_.find(channel_key);
  if (it == channel_groups_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}

#pragma once

#include <SDL2/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::platform {

class PendingWindowEventQueue {
 public:
  struct Entry {
    SDL_Event event{};
    std::uint32_t enqueue_tick_ms = 0;
  };

  static constexpr std::size_t kStorageSlotCount = 16;
  static constexpr std::size_t kMaxPendingEvents = kStorageSlotCount - 1;

  void Enqueue(const SDL_Event& event, std::uint32_t enqueue_tick_ms) {
    const std::size_t next_tail = NextIndex(tail_);
    if (next_tail == head_) {
      head_ = NextIndex(head_);
    }

    entries_[tail_].event = event;
    entries_[tail_].enqueue_tick_ms = enqueue_tick_ms;
    tail_ = next_tail;
  }

  [[nodiscard]] bool TryDequeue(Entry& entry) {
    if (head_ == tail_) {
      return false;
    }

    entry = entries_[head_];
    head_ = NextIndex(head_);
    return true;
  }

  [[nodiscard]] bool Empty() const {
    return head_ == tail_;
  }

  [[nodiscard]] std::size_t Size() const {
    if (tail_ >= head_) {
      return tail_ - head_;
    }

    return kStorageSlotCount - head_ + tail_;
  }

  void Clear() {
    head_ = 0;
    tail_ = 0;
  }

 private:
  [[nodiscard]] static constexpr std::size_t NextIndex(const std::size_t index) {
    return (index + 1) % kStorageSlotCount;
  }

  std::array<Entry, kStorageSlotCount> entries_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
};

}

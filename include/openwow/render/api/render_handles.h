#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace openwow::render::api {

constexpr std::uint32_t kInvalidHandleIndex = std::numeric_limits<std::uint32_t>::max();

template <typename Tag>
struct ResourceHandle {
  std::uint32_t index = kInvalidHandleIndex;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool IsValid() const noexcept {
    return index != kInvalidHandleIndex && generation != 0;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return IsValid();
  }

  [[nodiscard]] friend constexpr bool operator==(ResourceHandle lhs,
                                                 ResourceHandle rhs) noexcept {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
  }

  [[nodiscard]] friend constexpr bool operator!=(ResourceHandle lhs,
                                                 ResourceHandle rhs) noexcept {
    return !(lhs == rhs);
  }
};

struct TextureHandleTag;
struct BufferHandleTag;
struct ProgramHandleTag;
struct FramebufferHandleTag;
struct SamplerHandleTag;

using TextureHandle = ResourceHandle<TextureHandleTag>;
using BufferHandle = ResourceHandle<BufferHandleTag>;
using ProgramHandle = ResourceHandle<ProgramHandleTag>;
using FramebufferHandle = ResourceHandle<FramebufferHandleTag>;
using SamplerHandle = ResourceHandle<SamplerHandleTag>;

template <typename Handle>
class GenerationPool {
 public:
  [[nodiscard]] Handle Allocate() {
    if (free_head_ != kInvalidHandleIndex) {
      const std::uint32_t index = free_head_;
      Slot& slot = slots_[index];
      free_head_ = slot.next_free;
      slot.alive = true;
      slot.next_free = kInvalidHandleIndex;
      return Handle{index, slot.generation};
    }

    Slot slot;
    slot.alive = true;
    slots_.push_back(slot);
    const std::uint32_t index = static_cast<std::uint32_t>(slots_.size() - 1);
    return Handle{index, slots_.back().generation};
  }

  [[nodiscard]] bool Release(Handle handle) {
    if (!IsAlive(handle)) {
      return false;
    }

    Slot& slot = slots_[handle.index];
    slot.alive = false;
    ++slot.generation;
    if (slot.generation == 0) {
      ++slot.generation;
    }
    slot.next_free = free_head_;
    free_head_ = handle.index;
    return true;
  }

  [[nodiscard]] bool IsAlive(Handle handle) const {
    return handle.IsValid() && handle.index < slots_.size() && slots_[handle.index].alive &&
           slots_[handle.index].generation == handle.generation;
  }

  [[nodiscard]] std::size_t SlotCount() const noexcept {
    return slots_.size();
  }

 private:
  struct Slot {
    std::uint32_t generation = 1;
    bool alive = false;
    std::uint32_t next_free = kInvalidHandleIndex;
  };

  std::vector<Slot> slots_;
  std::uint32_t free_head_ = kInvalidHandleIndex;
};

}

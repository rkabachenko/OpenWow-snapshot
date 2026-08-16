#pragma once

#include "openwow/core/storm_containers.h"
#include "openwow/core/storm_string.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openwow::ui::widgets {

class CSimpleBatchedMesh {
 public:
  static constexpr std::uint32_t kEntrySize = 0x3C;
  static constexpr std::uint32_t kTailFloatOffset = 0x34;
  static constexpr std::uint32_t kTailFloatCount = 2;

  CSimpleBatchedMesh() = default;
  ~CSimpleBatchedMesh() { Release(); }

  CSimpleBatchedMesh(const CSimpleBatchedMesh&) = delete;
  CSimpleBatchedMesh& operator=(const CSimpleBatchedMesh&) = delete;

  CSimpleBatchedMesh(CSimpleBatchedMesh&& other) noexcept { MoveFrom(other); }

  CSimpleBatchedMesh& operator=(CSimpleBatchedMesh&& other) noexcept {
    if (this != &other) {
      Release();
      MoveFrom(other);
    }
    return *this;
  }

  std::uint8_t* ResizeCapacity(std::uint32_t new_capacity) {
    void* const old_data = data_;
    capacity_ = new_capacity;

    void* new_data = openwow::core::SMemReAlloc(
        old_data, static_cast<std::size_t>(kEntrySize) * new_capacity,
        kStormTag, -2, 16);
    data_ = static_cast<std::byte*>(new_data);

    if (new_data != nullptr) {
      return static_cast<std::uint8_t*>(new_data);
    }

    new_data = openwow::core::SMemAlloc(
        static_cast<std::size_t>(kEntrySize) * new_capacity, kStormTag, -2, 0);
    data_ = static_cast<std::byte*>(new_data);

    if (old_data != nullptr) {
      if (new_data != nullptr) {
        const auto copy_count = std::min(new_capacity, count_);
        for (std::uint32_t index = 0; index < copy_count; ++index) {
          std::memcpy(static_cast<std::byte*>(new_data) + index * kEntrySize,
                      static_cast<std::byte*>(old_data) + index * kEntrySize,
                      kEntrySize);
        }
      }

      openwow::core::SMemFree(old_data, kStormTag, -2, 0);
    }

    return static_cast<std::uint8_t*>(new_data);
  }

  std::uint8_t* Resize(std::uint32_t new_capacity) {
    return ResizeCapacity(new_capacity);
  }

  void EnsureCount(std::uint32_t new_count) {
    if (new_count <= count_) {
      count_ = new_count;
      return;
    }

    if (new_count > capacity_) {
      auto growth_quantum = growth_quantum_;
      if (growth_quantum == 0) {
        growth_quantum = ResolveGrowthQuantum(new_count);
      }

      std::uint32_t rounded_capacity = new_count;
      const auto remainder = new_count % growth_quantum;
      if (remainder != 0) {
        rounded_capacity = new_count + growth_quantum - remainder;
      }

      ResizeCapacity(rounded_capacity);
    }

    ZeroNewEntryTails(count_, new_count);
    count_ = new_count;
  }

  [[nodiscard]] std::uint32_t Capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint32_t Count() const noexcept { return count_; }
  [[nodiscard]] std::uint32_t GrowthQuantum() const noexcept {
    return growth_quantum_;
  }

  [[nodiscard]] std::uint32_t GetCapacity() const noexcept { return Capacity(); }
  [[nodiscard]] std::uint32_t GetCount() const noexcept { return Count(); }
  [[nodiscard]] std::uint32_t GetGrowthQuantum() const noexcept {
    return GrowthQuantum();
  }

  void SetCount(std::uint32_t value) noexcept { count_ = value; }

  [[nodiscard]] std::uint8_t* Data() noexcept {
    return reinterpret_cast<std::uint8_t*>(data_);
  }
  [[nodiscard]] const std::uint8_t* Data() const noexcept {
    return reinterpret_cast<const std::uint8_t*>(data_);
  }
  [[nodiscard]] std::uint8_t* GetData() noexcept { return Data(); }
  [[nodiscard]] const std::uint8_t* GetData() const noexcept { return Data(); }

  [[nodiscard]] std::uint8_t* Entry(std::uint32_t index) noexcept {
    if (data_ == nullptr || index >= capacity_) {
      return nullptr;
    }
    return reinterpret_cast<std::uint8_t*>(
        data_ + static_cast<std::size_t>(index) * kEntrySize);
  }

  [[nodiscard]] const std::uint8_t* Entry(std::uint32_t index) const noexcept {
    if (data_ == nullptr || index >= capacity_) {
      return nullptr;
    }
    return reinterpret_cast<const std::uint8_t*>(
        data_ + static_cast<std::size_t>(index) * kEntrySize);
  }

  [[nodiscard]] std::uint8_t* GetEntry(std::uint32_t index) noexcept {
    return Entry(index);
  }

  [[nodiscard]] const std::uint8_t* GetEntry(std::uint32_t index) const noexcept {
    return Entry(index);
  }

 private:
  static constexpr const char* kStormTag = ".?AUCSimpleBatchedMesh@@";

  std::uint32_t ResolveGrowthQuantum(std::uint32_t requested_count) noexcept {
    if (requested_count >= 8u) {
      growth_quantum_ = 8u;
    }

    return openwow::core::ResolveTSGrowableArrayAutoGrowQuantum<8>(
        requested_count);
  }

  void ZeroNewEntryTails(std::uint32_t first_new_entry,
                         std::uint32_t new_count) noexcept {
    if (data_ == nullptr) {
      return;
    }

    for (std::uint32_t index = first_new_entry; index < new_count; ++index) {
      auto* entry = data_ + static_cast<std::size_t>(index) * kEntrySize;
      std::memset(entry + kTailFloatOffset, 0,
                  sizeof(float) * kTailFloatCount);
    }
  }

  void MoveFrom(CSimpleBatchedMesh& other) noexcept {
    capacity_ = other.capacity_;
    count_ = other.count_;
    data_ = other.data_;
    growth_quantum_ = other.growth_quantum_;

    other.capacity_ = 0;
    other.count_ = 0;
    other.data_ = nullptr;
    other.growth_quantum_ = 0;
  }

  void Release() noexcept {
    if (data_ != nullptr) {
      openwow::core::SMemFree(data_, kStormTag, -2, 0);
      data_ = nullptr;
    }
    capacity_ = 0;
    count_ = 0;
    growth_quantum_ = 0;
  }

  std::uint32_t capacity_ = 0;
  std::uint32_t count_ = 0;
  std::byte* data_ = nullptr;
  std::uint32_t growth_quantum_ = 0;
};

}

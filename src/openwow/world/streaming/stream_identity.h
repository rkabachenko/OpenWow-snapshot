#pragma once

#include <cstdint>

namespace openwow::world {

struct MapGeneration {
  std::uint64_t value{1};

  [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
  friend constexpr bool operator==(MapGeneration, MapGeneration) = default;
};

inline MapGeneration& operator++(MapGeneration& generation) noexcept {
  ++generation.value;
  if (!generation.IsValid()) {
    ++generation.value;
  }
  return generation;
}

struct StreamOwnerHandle {
  std::uint64_t value{0};

  [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
  friend constexpr bool operator==(StreamOwnerHandle, StreamOwnerHandle) = default;
};

[[nodiscard]] constexpr StreamOwnerHandle
MakeStreamOwnerHandle(const std::uint64_t request_id) noexcept {
  return StreamOwnerHandle{request_id};
}

}

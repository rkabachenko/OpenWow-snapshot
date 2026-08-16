#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace openwow::render::m2 {

struct M2TransparentDrawDepth {
  static constexpr unsigned kDrawOrdinalBits = 12u;
  static constexpr unsigned kInstanceOrdinalBits = 32u - kDrawOrdinalBits;
  static constexpr std::uint32_t kMaxDrawOrdinal =
      (std::uint32_t{1} << kDrawOrdinalBits) - 1u;
  static constexpr std::uint32_t kMaxInstanceOrdinal =
      (std::uint32_t{1} << kInstanceOrdinalBits) - 1u;

  [[nodiscard]] static constexpr std::uint32_t Encode(
      const std::uint32_t instance_ordinal,
      const std::uint32_t draw_ordinal) noexcept {
    return (std::min(instance_ordinal, kMaxInstanceOrdinal) << kDrawOrdinalBits) |
           std::min(draw_ordinal, kMaxDrawOrdinal);
  }
};

class M2TransparentDrawOrder {
 public:

  [[nodiscard]] std::uint32_t Reserve(const std::uint32_t count = 1u) noexcept {
    const std::uint32_t first = next_ordinal_;
    next_ordinal_ += count;
    return first;
  }

  [[nodiscard]] std::uint32_t next_ordinal() const noexcept {
    return next_ordinal_;
  }

 private:
  std::uint32_t next_ordinal_ = 0u;
};

class M2TransparentDrawOrdinalScope {
 public:
  explicit M2TransparentDrawOrdinalScope(
      const std::span<const std::uint32_t> ordinals) noexcept
      : previous_(bound_) {
    bound_ = ordinals;
  }
  ~M2TransparentDrawOrdinalScope() { bound_ = previous_; }
  M2TransparentDrawOrdinalScope(const M2TransparentDrawOrdinalScope&) = delete;
  M2TransparentDrawOrdinalScope& operator=(const M2TransparentDrawOrdinalScope&) =
      delete;

  [[nodiscard]] static std::span<const std::uint32_t> Current() noexcept {
    return bound_;
  }

 private:
  static inline thread_local std::span<const std::uint32_t> bound_{};
  std::span<const std::uint32_t> previous_;
};

class M2InstanceDrawSortDepth {
 public:
  explicit M2InstanceDrawSortDepth(const std::uint32_t instance_ordinal) noexcept
      : instance_ordinal_(instance_ordinal) {}

  [[nodiscard]] std::uint32_t NextDrawDepth() noexcept {
    return M2TransparentDrawDepth::Encode(instance_ordinal_, next_draw_ordinal_++);
  }

  [[nodiscard]] std::uint32_t instance_ordinal() const noexcept {
    return instance_ordinal_;
  }

 private:
  std::uint32_t instance_ordinal_;
  std::uint32_t next_draw_ordinal_ = 0u;
};

}

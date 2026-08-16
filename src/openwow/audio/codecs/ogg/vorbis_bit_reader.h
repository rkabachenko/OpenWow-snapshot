#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::audio::detail {

class VorbisBitReader {
public:
  explicit VorbisBitReader(std::span<const std::uint8_t> bytes)
      : bytes_(bytes), total_bytes_(bytes.size()) {}

  [[nodiscard]] std::size_t bit_offset() const {
    return byte_cursor_ * 8u + bit_shift_;
  }

  [[nodiscard]] bool HasBits(const std::size_t bit_count) const {
    const auto total_bits = total_bytes_ * 8u;
    const auto consumed_bits =
        byte_cursor_ >= total_bytes_ ? total_bits : bit_offset();
    return bit_count <= total_bits - consumed_bits;
  }

  [[nodiscard]] bool ReadBits(const std::uint32_t bit_count,
                              std::uint32_t *value) {
    if (!value || bit_count > 32u) {
      return false;
    }
    if (bit_count == 0u) {
      *value = 0;
      return true;
    }

    const bool can_read = CanReadBits(bit_count);
    std::uint32_t raw_value = 0;
    if (can_read) {
      raw_value = PeekBitsUnchecked(bit_count);
    }
    Advance(bit_count);

    if (!can_read) {
      return false;
    }
    *value = raw_value;
    return true;
  }

  [[nodiscard]] bool SkipBits(const std::size_t bit_count) {
    if (!HasBits(bit_count)) {
      return false;
    }

    Advance(bit_count);
    return true;
  }

private:
  static constexpr std::array<std::uint32_t, 33> BuildLowerBitMasks32() {
    std::array<std::uint32_t, 33> masks{};
    for (std::size_t i = 1; i < masks.size(); ++i) {
      masks[i] = i == 32 ? 0xFFFFFFFFu
                         : ((static_cast<std::uint32_t>(1u) << i) - 1u);
    }
    return masks;
  }

  [[nodiscard]] bool CanReadBits(const std::uint32_t bit_count) const {
    if (byte_cursor_ < total_bytes_ && total_bytes_ - byte_cursor_ > 4u) {
      return true;
    }

    const auto requested_bits =
        byte_cursor_ * 8u + bit_shift_ + static_cast<std::size_t>(bit_count);
    return requested_bits <= total_bytes_ * 8u;
  }

  [[nodiscard]] std::uint32_t PeekBitsUnchecked(
      const std::uint32_t bit_count) const {
    const auto shift = static_cast<unsigned>(bit_shift_);
    const auto total_shift = bit_shift_ + static_cast<std::size_t>(bit_count);

    std::uint32_t value = static_cast<std::uint32_t>(bytes_[byte_cursor_] >> shift);
    if (total_shift > 8u) {
      value |= static_cast<std::uint32_t>(bytes_[byte_cursor_ + 1u])
               << (8u - shift);
      if (total_shift > 16u) {
        value |= static_cast<std::uint32_t>(bytes_[byte_cursor_ + 2u])
                 << (16u - shift);
        if (total_shift > 24u) {
          value |= static_cast<std::uint32_t>(bytes_[byte_cursor_ + 3u])
                   << (24u - shift);
          if (total_shift > 32u && shift != 0u) {
            value |= static_cast<std::uint32_t>(bytes_[byte_cursor_ + 4u])
                     << (32u - shift);
          }
        }
      }
    }

    return value & kLowerBitMasks32[bit_count];
  }

  void Advance(const std::size_t bit_count) {
    const auto total_shift = bit_shift_ + bit_count;
    byte_cursor_ += total_shift / 8u;
    bit_shift_ = total_shift & 7u;
  }

  static constexpr auto kLowerBitMasks32 = []() constexpr {
    std::array<std::uint32_t, 33> masks{};
    for (std::size_t i = 1; i < masks.size(); ++i) {
      masks[i] = i == 32 ? 0xFFFFFFFFu
                         : ((static_cast<std::uint32_t>(1u) << i) - 1u);
    }
    return masks;
  }();

  std::span<const std::uint8_t> bytes_;
  std::size_t total_bytes_{0};
  std::size_t byte_cursor_{0};
  std::size_t bit_shift_{0};
};

}

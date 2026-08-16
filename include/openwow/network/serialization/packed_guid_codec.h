#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace openwow::net {

struct PackedGuidEncoding {
  std::array<std::uint8_t, 9> bytes{};
  std::uint8_t size{1};

  [[nodiscard]] constexpr std::span<const std::uint8_t> view() const noexcept {
    return {bytes.data(), size};
  }
};

struct PackedGuidDecodeResult {
  std::uint64_t value{0};
  std::size_t bytes_consumed{0};

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return bytes_consumed != 0;
  }
};

[[nodiscard]] constexpr std::uint8_t PackedGuidMask(
    const std::uint64_t guid) noexcept {
  std::uint8_t mask = 0;
  for (std::uint8_t index = 0; index < 8; ++index) {
    if (static_cast<std::uint8_t>(guid >> (index * 8u)) != 0) {
      mask |= static_cast<std::uint8_t>(1u << index);
    }
  }
  return mask;
}

[[nodiscard]] constexpr std::uint8_t PackedGuidSourceByte(
    const std::uint64_t guid,
    const std::uint8_t index) noexcept {
  return static_cast<std::uint8_t>(guid >> (index * 8u));
}

[[nodiscard]] constexpr PackedGuidEncoding EncodePackedGuid(
    const std::uint64_t guid) noexcept {
  PackedGuidEncoding encoded{};
  encoded.bytes[0] = PackedGuidMask(guid);
  for (std::uint8_t index = 0; index < 8; ++index) {
    const std::uint8_t value = PackedGuidSourceByte(guid, index);
    if (value != 0) {
      encoded.bytes[encoded.size++] = value;
    }
  }
  return encoded;
}

[[nodiscard]] constexpr PackedGuidDecodeResult DecodePackedGuid(
    const std::span<const std::uint8_t> input) noexcept {
  if (input.empty()) {
    return {};
  }

  const std::uint8_t mask = input.front();
  std::size_t cursor = 1;
  std::uint64_t guid = 0;
  for (std::uint8_t index = 0; index < 8; ++index) {
    if ((mask & static_cast<std::uint8_t>(1u << index)) == 0) {
      continue;
    }
    if (cursor >= input.size()) {
      return {};
    }
    guid |= static_cast<std::uint64_t>(input[cursor++]) << (index * 8u);
  }
  return {.value = guid, .bytes_consumed = cursor};
}

[[nodiscard]] constexpr PackedGuidDecodeResult DecodePackedGuid(
    const std::uint8_t* const data,
    const std::size_t size) noexcept {
  if (!data) {
    return {};
  }
  return DecodePackedGuid(std::span<const std::uint8_t>{data, size});
}

}

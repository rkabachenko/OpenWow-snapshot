#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data::model {

class BinaryReader {
 public:
  BinaryReader() = default;
  BinaryReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  std::size_t size() const { return size_; }

  bool CanRead(std::size_t offset, std::size_t bytes) const {
    if (data_ == nullptr) return false;
    if (offset > size_) return false;
    return bytes <= (size_ - offset);
  }

  std::optional<std::uint8_t> ReadU8(std::size_t offset) const {
    if (!CanRead(offset, 1)) return std::nullopt;
    return data_[offset];
  }

  std::optional<std::uint16_t> ReadU16(std::size_t offset) const {
    if (!CanRead(offset, 2)) return std::nullopt;
    const std::uint16_t v0 = static_cast<std::uint16_t>(data_[offset + 0]);
    const std::uint16_t v1 = static_cast<std::uint16_t>(data_[offset + 1]) << 8;
    return static_cast<std::uint16_t>(v0 | v1);
  }

  std::optional<std::uint32_t> ReadU32(std::size_t offset) const {
    if (!CanRead(offset, 4)) return std::nullopt;
    const std::uint32_t b0 = static_cast<std::uint32_t>(data_[offset + 0]);
    const std::uint32_t b1 = static_cast<std::uint32_t>(data_[offset + 1]) << 8;
    const std::uint32_t b2 = static_cast<std::uint32_t>(data_[offset + 2]) << 16;
    const std::uint32_t b3 = static_cast<std::uint32_t>(data_[offset + 3]) << 24;
    return b0 | b1 | b2 | b3;
  }

  std::optional<std::int16_t> ReadI16(std::size_t offset) const {
    const auto v = ReadU16(offset);
    if (!v.has_value()) return std::nullopt;
    return static_cast<std::int16_t>(*v);
  }

  std::optional<std::int32_t> ReadI32(std::size_t offset) const {
    const auto v = ReadU32(offset);
    if (!v.has_value()) return std::nullopt;
    return static_cast<std::int32_t>(*v);
  }

  std::optional<float> ReadF32(std::size_t offset) const {
    const auto raw = ReadU32(offset);
    if (!raw.has_value()) return std::nullopt;
    float out = 0.0F;
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(&out, &(*raw), sizeof(float));
    return out;
  }

  std::optional<std::span<const std::uint8_t>> ReadBytes(std::size_t offset, std::size_t bytes) const {
    if (!CanRead(offset, bytes)) return std::nullopt;
    return std::span<const std::uint8_t>(data_ + offset, bytes);
  }

  std::optional<std::string> ReadCString(std::size_t offset, std::size_t max_bytes) const {
    if (!CanRead(offset, 1)) return std::nullopt;
    const std::size_t limit = std::min(size_, offset + max_bytes);
    std::string out;
    for (std::size_t i = offset; i < limit; ++i) {
      const char ch = static_cast<char>(data_[i]);
      if (ch == '\0') break;
      out.push_back(ch);
    }
    return out;
  }

  std::optional<std::string> ReadString(std::size_t offset, std::size_t bytes) const {
    const auto span = ReadBytes(offset, bytes);
    if (!span.has_value()) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(span->data()), span->size());
  }

  template <typename T>
  std::optional<std::span<const T>> ReadSpan(std::size_t offset, std::size_t count) const {
    if (count == 0) {
      return std::span<const T>();
    }
    const std::size_t bytes = count * sizeof(T);
    if (!CanRead(offset, bytes)) return std::nullopt;
    const auto* ptr = reinterpret_cast<const T*>(data_ + offset);
    return std::span<const T>(ptr, count);
  }

  template <typename T>
  std::optional<std::vector<T>> ReadVector(std::size_t offset, std::size_t count) const {
    const auto span = ReadSpan<T>(offset, count);
    if (!span.has_value()) return std::nullopt;
    return std::vector<T>(span->begin(), span->end());
  }

 private:
  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
};

}

#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace openwow::game {

class PacketReader {
 public:
  PacketReader() = default;
  PacketReader(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size), pos_(0) {}
  explicit PacketReader(std::span<const std::uint8_t> buf)
      : data_(buf.data()), size_(buf.size()), pos_(0) {}

  [[nodiscard]] std::size_t Position() const { return pos_; }
  [[nodiscard]] std::size_t Remaining() const { return size_ - pos_; }
  [[nodiscard]] bool Good() const { return pos_ <= size_; }
  [[nodiscard]] bool HasBytes(std::size_t n) const { return Remaining() >= n; }

  void Skip(std::size_t n) {
    pos_ += n;
    if (pos_ > size_) pos_ = size_;
  }

  [[nodiscard]] bool ReadU8(std::uint8_t& out) {
    if (!HasBytes(1)) return false;
    out = data_[pos_++];
    return true;
  }

  [[nodiscard]] bool ReadU16(std::uint16_t& out) {
    if (!HasBytes(2)) return false;
    std::memcpy(&out, data_ + pos_, 2);
    pos_ += 2;
    return true;
  }

  [[nodiscard]] bool ReadU32(std::uint32_t& out) {
    if (!HasBytes(4)) return false;
    std::memcpy(&out, data_ + pos_, 4);
    pos_ += 4;
    return true;
  }

  [[nodiscard]] bool ReadI32(std::int32_t& out) {
    if (!HasBytes(4)) return false;
    std::memcpy(&out, data_ + pos_, 4);
    pos_ += 4;
    return true;
  }

  [[nodiscard]] bool ReadU64(std::uint64_t& out) {
    if (!HasBytes(8)) return false;
    std::memcpy(&out, data_ + pos_, 8);
    pos_ += 8;
    return true;
  }

  [[nodiscard]] bool ReadI64(std::int64_t& out) {
    if (!HasBytes(8)) return false;
    std::memcpy(&out, data_ + pos_, 8);
    pos_ += 8;
    return true;
  }

  [[nodiscard]] bool ReadFloat(float& out) {
    if (!HasBytes(4)) return false;
    std::memcpy(&out, data_ + pos_, 4);
    pos_ += 4;
    return true;
  }

  [[nodiscard]] bool ReadPackedGuid(ObjectGuid& out) {
    auto consumed = ObjectGuid::Unpack(data_ + pos_, Remaining(), out);
    if (consumed == 0) return false;
    pos_ += consumed;
    return true;
  }

  [[nodiscard]] bool ReadGuid(ObjectGuid& out) {
    std::uint64_t raw;
    if (!ReadU64(raw)) return false;
    out = ObjectGuid(raw);
    return true;
  }

  [[nodiscard]] bool ReadCString(std::string& out) {
    out.clear();
    while (pos_ < size_) {
      char c = static_cast<char>(data_[pos_++]);
      if (c == '\0') return true;
      out += c;
    }
    return false;
  }

  [[nodiscard]] bool ReadCString(std::string& out,
                                 const std::size_t max_bytes_including_nul) {
    out.clear();
    if (max_bytes_including_nul == 0) {
      return false;
    }

    for (std::size_t consumed = 0; consumed < max_bytes_including_nul;
         ++consumed) {
      std::uint8_t byte = 0;
      if (!ReadU8(byte)) {
        out.clear();
        return false;
      }
      if (byte == 0) {
        return true;
      }
      out.push_back(static_cast<char>(byte));
    }

    out.clear();

    pos_ = size_;
    return false;
  }

  [[nodiscard]] const std::uint8_t* PeekBytes(std::size_t n) const {
    if (!HasBytes(n)) return nullptr;
    return data_ + pos_;
  }

  [[nodiscard]] bool ReadBytes(std::uint8_t* dst, std::size_t n) {
    if (!HasBytes(n)) return false;
    std::memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return true;
  }

 private:
  const std::uint8_t* data_{nullptr};
  std::size_t size_{0};
  std::size_t pos_{0};
};

}

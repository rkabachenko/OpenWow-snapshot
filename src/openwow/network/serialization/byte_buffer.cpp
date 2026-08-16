#include "openwow/network/serialization/byte_buffer.h"
#include "openwow/network/serialization/packed_guid_codec.h"

#include <bit>
#include <cstring>

namespace openwow::net {

void ByteBuffer::WriteUInt8(uint8_t value) {
  data_.push_back(value);
}

void ByteBuffer::WriteUInt16(uint16_t value) {
  data_.push_back(static_cast<uint8_t>(value & 0xFF));
  data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void ByteBuffer::WriteUInt32(uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    data_.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void ByteBuffer::WriteUInt64(uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    data_.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void ByteBuffer::WriteInt8(int8_t value) {
  data_.push_back(static_cast<uint8_t>(value));
}

void ByteBuffer::WriteInt16(int16_t value) {
  WriteUInt16(static_cast<uint16_t>(value));
}

void ByteBuffer::WriteInt32(int32_t value) {
  WriteUInt32(static_cast<uint32_t>(value));
}

void ByteBuffer::WriteFloat(float value) {
  WriteUInt32(std::bit_cast<std::uint32_t>(value));
}

void ByteBuffer::WriteString(const std::string& value) {
  data_.insert(data_.end(), value.begin(), value.end());
  data_.push_back(0);
}

void ByteBuffer::WriteBytes(const uint8_t* data, std::size_t len) {
  if (data && len > 0) {
    data_.insert(data_.end(), data, data + len);
  }
}

void ByteBuffer::WritePackedGuid(uint64_t guid) {
  const auto encoded = EncodePackedGuid(guid);
  data_.insert(data_.end(), encoded.view().begin(), encoded.view().end());
}

bool ByteBuffer::ReadUInt8(uint8_t* out) {
  if (!out || !CanRead(1)) return false;
  *out = data_[read_offset_++];
  return true;
}

bool ByteBuffer::ReadUInt16(uint16_t* out) {
  if (!out || !CanRead(2)) return false;
  const uint16_t lo = data_[read_offset_++];
  const uint16_t hi = data_[read_offset_++];
  *out = static_cast<uint16_t>((hi << 8) | lo);
  return true;
}

bool ByteBuffer::ReadUInt32(uint32_t* out) {
  if (!out || !CanRead(4)) return false;
  *out = static_cast<std::uint32_t>(data_[read_offset_])
       | static_cast<std::uint32_t>(data_[read_offset_ + 1]) << 8
       | static_cast<std::uint32_t>(data_[read_offset_ + 2]) << 16
       | static_cast<std::uint32_t>(data_[read_offset_ + 3]) << 24;
  read_offset_ += 4;
  return true;
}

bool ByteBuffer::ReadUInt64(uint64_t* out) {
  if (!out || !CanRead(8)) return false;
  std::uint64_t value = 0;
  for (unsigned index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(data_[read_offset_ + index])
             << (index * 8u);
  }
  *out = value;
  read_offset_ += 8;
  return true;
}

bool ByteBuffer::ReadInt8(int8_t* out) {
  if (!out) return false;
  std::uint8_t bits = 0;
  if (!ReadUInt8(&bits)) return false;
  *out = std::bit_cast<std::int8_t>(bits);
  return true;
}

bool ByteBuffer::ReadInt16(int16_t* out) {
  if (!out) return false;
  std::uint16_t bits = 0;
  if (!ReadUInt16(&bits)) return false;
  *out = std::bit_cast<std::int16_t>(bits);
  return true;
}

bool ByteBuffer::ReadInt32(int32_t* out) {
  if (!out) return false;
  std::uint32_t bits = 0;
  if (!ReadUInt32(&bits)) return false;
  *out = std::bit_cast<std::int32_t>(bits);
  return true;
}

bool ByteBuffer::ReadFloat(float* out) {
  if (!out) return false;
  std::uint32_t bits = 0;
  if (!ReadUInt32(&bits)) return false;
  *out = std::bit_cast<float>(bits);
  return true;
}

bool ByteBuffer::ReadString(std::string& out) {
  out.clear();
  while (read_offset_ < data_.size()) {
    uint8_t ch = data_[read_offset_++];
    if (ch == 0) return true;
    out.push_back(static_cast<char>(ch));
  }
  return true;
}

bool ByteBuffer::ReadBytes(uint8_t* out, std::size_t len) {
  if (!out || !CanRead(len)) return false;
  std::memcpy(out, data_.data() + read_offset_, len);
  read_offset_ += len;
  return true;
}

bool ByteBuffer::ReadPackedGuid(uint64_t* out) {
  if (!out || GetRemainingBytes() == 0) return false;
  const auto decoded = DecodePackedGuid(
      data_.data() + read_offset_, GetRemainingBytes());
  if (!decoded) return false;
  read_offset_ += decoded.bytes_consumed;
  *out = decoded.value;
  return true;
}

const std::vector<uint8_t>& ByteBuffer::Data() const {
  return data_;
}

std::size_t ByteBuffer::GetSize() const {
  return data_.size();
}

std::size_t ByteBuffer::GetReadOffset() const {
  return read_offset_;
}

std::size_t ByteBuffer::GetRemainingBytes() const {
  return (read_offset_ < data_.size()) ? data_.size() - read_offset_ : 0;
}

bool ByteBuffer::IsEmpty() const {
  return data_.empty();
}

bool ByteBuffer::CanRead(std::size_t bytes) const {
  return read_offset_ + bytes <= data_.size();
}

void ByteBuffer::ResetReadOffset() {
  read_offset_ = 0;
}

bool ByteBuffer::IsFullyConsumed() const {
  return read_offset_ == data_.size();
}

void ByteBuffer::Clear() {
  data_.clear();
  read_offset_ = 0;
}

void ByteBuffer::Reserve(std::size_t capacity) {
  data_.reserve(capacity);
}

void ByteBuffer::Append(const ByteBuffer& other) {
  data_.insert(data_.end(), other.data_.begin(), other.data_.end());
}

std::vector<uint8_t> ByteBuffer::Detach() {
  auto result = std::move(data_);
  data_.clear();
  read_offset_ = 0;
  return result;
}

}

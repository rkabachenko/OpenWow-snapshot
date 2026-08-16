#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::net {

class ByteBuffer {
 public:
  void WriteUInt8(uint8_t value);
  void WriteUInt16(uint16_t value);
  void WriteUInt32(uint32_t value);
  void WriteUInt64(uint64_t value);
  void WriteInt8(int8_t value);
  void WriteInt16(int16_t value);
  void WriteInt32(int32_t value);
  void WriteFloat(float value);
  void WriteString(const std::string& value);
  void WriteBytes(const uint8_t* data, std::size_t len);
  void WritePackedGuid(uint64_t guid);

  bool ReadUInt8(uint8_t* out);
  bool ReadUInt16(uint16_t* out);
  bool ReadUInt32(uint32_t* out);
  bool ReadUInt64(uint64_t* out);
  bool ReadInt8(int8_t* out);
  bool ReadInt16(int16_t* out);
  bool ReadInt32(int32_t* out);
  bool ReadFloat(float* out);
  bool ReadString(std::string& out);
  bool ReadBytes(uint8_t* out, std::size_t len);
  bool ReadPackedGuid(uint64_t* out);

  [[nodiscard]] const std::vector<uint8_t>& Data() const;
  [[nodiscard]] std::size_t GetSize() const;
  [[nodiscard]] std::size_t GetReadOffset() const;
  [[nodiscard]] std::size_t GetRemainingBytes() const;
  [[nodiscard]] bool IsEmpty() const;
  [[nodiscard]] bool CanRead(std::size_t bytes) const;

  [[nodiscard]] bool IsFullyConsumed() const;

  void ResetReadOffset();
  void Clear();
  void Reserve(std::size_t capacity);
  void Append(const ByteBuffer& other);

  [[nodiscard]] std::vector<uint8_t> Detach();

 private:
  std::vector<uint8_t> data_;
  std::size_t read_offset_{0};
};

}

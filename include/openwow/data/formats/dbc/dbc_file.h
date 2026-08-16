#pragma once

#include "openwow/data/formats/dbc/dbc_locale.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openwow::data::dbc {

struct DbcHeader {
  static constexpr std::uint32_t kWdbcSignature = 0x43424457u;
  static constexpr std::size_t kWordSize = sizeof(std::uint32_t);
  static constexpr std::size_t kWordCount = 5u;
  static constexpr std::size_t kEncodedSize = kWordCount * kWordSize;

  std::uint32_t signature;
  std::uint32_t record_count;
  std::uint32_t field_count;
  std::uint32_t record_size;
  std::uint32_t string_block_size;
};

enum class DbcError {
  kOk,
  kTooSmall,
  kBadMagic,
  kInconsistentSize,
};

class DbcFile {
 public:
  DbcFile() = default;
  explicit DbcFile(DbcLocale locale) : locale_(locale) {}

  DbcError LoadFromBytes(std::vector<std::uint8_t> data);

  [[nodiscard]] bool loaded() const { return loaded_; }

  [[nodiscard]] std::uint32_t record_count() const {
    return loaded_ ? header_.record_count : 0;
  }
  [[nodiscard]] std::uint32_t field_count() const {
    return loaded_ ? header_.field_count : 0;
  }
  [[nodiscard]] std::uint32_t record_size() const {
    return loaded_ ? header_.record_size : 0;
  }

  [[nodiscard]] std::uint32_t GetUInt32(std::uint32_t record, std::uint32_t field) const;
  [[nodiscard]] std::int32_t GetInt32(std::uint32_t record, std::uint32_t field) const;
  [[nodiscard]] float GetFloat(std::uint32_t record, std::uint32_t field) const;

  [[nodiscard]] std::string_view GetString(std::uint32_t record, std::uint32_t field) const;
  [[nodiscard]] std::string_view GetLocalizedString(std::uint32_t record,
                                                    std::uint32_t first_field) const;

  [[nodiscard]] std::uint8_t GetByte(std::uint32_t record, std::uint32_t byte_offset) const;
  [[nodiscard]] std::uint32_t GetUInt32AtOffset(std::uint32_t record,
                                                std::uint32_t byte_offset) const;
  [[nodiscard]] std::int32_t GetInt32AtOffset(std::uint32_t record,
                                              std::uint32_t byte_offset) const;
  [[nodiscard]] float GetFloatAtOffset(std::uint32_t record,
                                       std::uint32_t byte_offset) const;
  [[nodiscard]] std::string_view GetStringAtOffset(std::uint32_t record,
                                                   std::uint32_t byte_offset) const;

  [[nodiscard]] static const char* GetErrorName(DbcError err);

 private:
  [[nodiscard]] const std::uint8_t* RecordPtr(std::uint32_t record) const;
  [[nodiscard]] const std::uint8_t* BytePtr(std::uint32_t record,
                                            std::uint32_t byte_offset,
                                            std::size_t byte_count) const;
  [[nodiscard]] const std::uint8_t* FieldPtr(std::uint32_t record,
                                             std::uint32_t field) const;
  [[nodiscard]] std::string_view StringAtOffset(std::uint32_t offset) const;

  std::vector<std::uint8_t> data_;
  DbcHeader header_{};
  std::size_t records_offset_ = 0;
  std::size_t string_block_offset_ = 0;
  DbcLocale locale_ = DbcLocale::kEnUs;
  bool loaded_ = false;
};

}

#include "openwow/data/formats/dbc/dbc_file.h"

#include <bit>
#include <cstring>
#include <limits>

namespace openwow::data::dbc {

namespace {

enum class DbcHeaderWord : std::size_t {
  kSignature,
  kRecordCount,
  kFieldCount,
  kRecordSize,
  kStringBlockSize,
};

constexpr std::size_t HeaderOffset(const DbcHeaderWord word) {
  return static_cast<std::size_t>(word) * DbcHeader::kWordSize;
}

std::uint32_t ReadLe32(const std::uint8_t* const bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

bool CheckedMultiply(const std::size_t left, const std::size_t right,
                     std::size_t* const result) {
  if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  *result = left * right;
  return true;
}

bool CheckedAdd(const std::size_t left, const std::size_t right,
                std::size_t* const result) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    return false;
  }
  *result = left + right;
  return true;
}

}

DbcError DbcFile::LoadFromBytes(std::vector<std::uint8_t> data) {
  data_.clear();
  header_ = {};
  records_offset_ = 0;
  string_block_offset_ = 0;
  loaded_ = false;

  if (data.size() < HeaderOffset(DbcHeaderWord::kRecordCount)) {
    return DbcError::kTooSmall;
  }

  const auto signature =
      ReadLe32(data.data() + HeaderOffset(DbcHeaderWord::kSignature));
  if (signature != DbcHeader::kWdbcSignature) {
    return DbcError::kBadMagic;
  }
  if (data.size() <
      HeaderOffset(DbcHeaderWord::kFieldCount)) {
    return DbcError::kTooSmall;
  }

  const auto record_count =
      ReadLe32(data.data() + HeaderOffset(DbcHeaderWord::kRecordCount));
  if (record_count == 0) {

    return DbcError::kOk;
  }

  if (data.size() < DbcHeader::kEncodedSize) {
    return DbcError::kTooSmall;
  }

  DbcHeader decoded{
      .signature = signature,
      .record_count = record_count,
      .field_count =
          ReadLe32(data.data() + HeaderOffset(DbcHeaderWord::kFieldCount)),
      .record_size =
          ReadLe32(data.data() + HeaderOffset(DbcHeaderWord::kRecordSize)),
      .string_block_size =
          ReadLe32(data.data() +
                   HeaderOffset(DbcHeaderWord::kStringBlockSize)),
  };

  std::size_t records_size = 0;
  std::size_t strings_offset = 0;
  std::size_t required_size = 0;
  if (!CheckedMultiply(decoded.record_count, decoded.record_size, &records_size) ||
      !CheckedAdd(DbcHeader::kEncodedSize, records_size, &strings_offset) ||
      !CheckedAdd(strings_offset, decoded.string_block_size, &required_size) ||
      required_size > data.size()) {
    return DbcError::kInconsistentSize;
  }

  data_ = std::move(data);
  header_ = decoded;
  records_offset_ = DbcHeader::kEncodedSize;
  string_block_offset_ = strings_offset;
  loaded_ = true;
  return DbcError::kOk;
}

const std::uint8_t* DbcFile::RecordPtr(const std::uint32_t record) const {
  if (!loaded_ || record >= header_.record_count) {
    return nullptr;
  }

  const auto row_offset = static_cast<std::size_t>(record) * header_.record_size;
  return data_.data() + records_offset_ + row_offset;
}

const std::uint8_t* DbcFile::BytePtr(const std::uint32_t record,
                                     const std::uint32_t byte_offset,
                                     const std::size_t byte_count) const {
  const auto* const row = RecordPtr(record);
  if (row == nullptr || byte_offset > header_.record_size ||
      byte_count > static_cast<std::size_t>(header_.record_size - byte_offset)) {
    return nullptr;
  }
  return row + byte_offset;
}

const std::uint8_t* DbcFile::FieldPtr(const std::uint32_t record,
                                      const std::uint32_t field) const {
  constexpr auto kFieldWidth =
      static_cast<std::uint32_t>(DbcHeader::kWordSize);
  if (!loaded_ || field >= header_.field_count ||
      field > std::numeric_limits<std::uint32_t>::max() /
                  kFieldWidth) {
    return nullptr;
  }
  return BytePtr(record, field * kFieldWidth, DbcHeader::kWordSize);
}

std::uint32_t DbcFile::GetUInt32(const std::uint32_t record,
                                 const std::uint32_t field) const {
  const auto* const bytes = FieldPtr(record, field);
  return bytes != nullptr ? ReadLe32(bytes) : 0u;
}

std::int32_t DbcFile::GetInt32(const std::uint32_t record,
                               const std::uint32_t field) const {
  return std::bit_cast<std::int32_t>(GetUInt32(record, field));
}

float DbcFile::GetFloat(const std::uint32_t record, const std::uint32_t field) const {
  const std::uint32_t bits = GetUInt32(record, field);
  float value = 0.0F;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string_view DbcFile::StringAtOffset(const std::uint32_t offset) const {
  if (!loaded_ || offset >= header_.string_block_size) {
    return {};
  }

  const char* const first =
      reinterpret_cast<const char*>(data_.data() + string_block_offset_ + offset);
  const auto remaining = static_cast<std::size_t>(header_.string_block_size - offset);
  const void* const terminator = std::memchr(first, '\0', remaining);
  if (terminator == nullptr) {
    return {};
  }

  const auto length = static_cast<std::size_t>(
      static_cast<const char*>(terminator) - first);
  return {first, length};
}

std::string_view DbcFile::GetString(const std::uint32_t record,
                                    const std::uint32_t field) const {
  if (FieldPtr(record, field) == nullptr) {
    return {};
  }
  return StringAtOffset(GetUInt32(record, field));
}

std::string_view DbcFile::GetLocalizedString(const std::uint32_t record,
                                             const std::uint32_t first_field) const {
  const auto localized_field = first_field + ToDbcLocaleIndex(locale_);

  return localized_field >= first_field && localized_field < header_.field_count
             ? GetString(record, localized_field)
             : std::string_view{};
}

std::uint8_t DbcFile::GetByte(const std::uint32_t record,
                              const std::uint32_t byte_offset) const {
  const auto* const bytes = BytePtr(record, byte_offset, 1u);
  return bytes != nullptr ? *bytes : 0u;
}

std::uint32_t DbcFile::GetUInt32AtOffset(const std::uint32_t record,
                                         const std::uint32_t byte_offset) const {
  const auto* const bytes =
      BytePtr(record, byte_offset, DbcHeader::kWordSize);
  return bytes != nullptr ? ReadLe32(bytes) : 0u;
}

std::int32_t DbcFile::GetInt32AtOffset(const std::uint32_t record,
                                       const std::uint32_t byte_offset) const {
  return std::bit_cast<std::int32_t>(
      GetUInt32AtOffset(record, byte_offset));
}

float DbcFile::GetFloatAtOffset(const std::uint32_t record,
                                const std::uint32_t byte_offset) const {
  const std::uint32_t bits = GetUInt32AtOffset(record, byte_offset);
  float value = 0.0F;
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string_view DbcFile::GetStringAtOffset(const std::uint32_t record,
                                            const std::uint32_t byte_offset) const {
  if (BytePtr(record, byte_offset, DbcHeader::kWordSize) == nullptr) {
    return {};
  }
  return StringAtOffset(GetUInt32AtOffset(record, byte_offset));
}

const char* DbcFile::GetErrorName(const DbcError error) {
  switch (error) {
  case DbcError::kOk:
    return "Ok";
  case DbcError::kTooSmall:
    return "TooSmall";
  case DbcError::kBadMagic:
    return "BadMagic";
  case DbcError::kInconsistentSize:
    return "InconsistentSize";
  }
  return "Unknown";
}

}

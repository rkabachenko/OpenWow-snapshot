#include "openwow/debug/capture/frame_replay_archive.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <utility>

#include <zlib.h>

namespace openwow::debug {
namespace {

constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034B50U;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014B50U;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054B50U;
constexpr std::size_t kLocalFileHeaderBytes = 30;
constexpr std::size_t kCentralDirectoryHeaderBytes = 46;
constexpr std::size_t kEndOfCentralDirectoryBytes = 22;
constexpr std::size_t kMaximumZipCommentBytes = 65'535;
constexpr std::size_t kIoBufferBytes = 64 * 1024;
constexpr std::uint16_t kZipVersion10 = 10;
constexpr std::uint16_t kZipVersion20 = 20;
constexpr std::uint16_t kZipMethodStored = 0;
constexpr std::uint16_t kZipMethodDeflate = 8;
constexpr std::uint16_t kCompressionOptionFlags = 3U << 1U;
constexpr std::uint16_t kDataDescriptorFlag = 1U << 3U;
constexpr std::uint16_t kUtf8Flag = 1U << 11U;
constexpr std::uint16_t kSupportedFlags =
    kCompressionOptionFlags | kDataDescriptorFlag | kUtf8Flag;

void ClearError(FrameReplayArchiveError* error) {
  if (error != nullptr) {
    *error = {};
  }
}

bool Fail(FrameReplayArchiveError* error,
          const FrameReplayArchiveErrorCode code, std::string detail) {
  if (error != nullptr) {
    error->code = code;
    error->detail = std::move(detail);
  }
  return false;
}

template <typename Value>
bool CheckedAdd(const Value lhs, const Value rhs, Value& result) noexcept {
  if (lhs > std::numeric_limits<Value>::max() - rhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

std::uint16_t ReadLittleU16(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadLittleU32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

bool ReadAt(std::ifstream& input, const std::uint64_t offset,
            const std::span<std::uint8_t> destination) {
  if (destination.empty()) {
    return true;
  }
  if (offset > static_cast<std::uint64_t>(
                   std::numeric_limits<std::streamoff>::max()) ||
      destination.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) {
    return false;
  }
  input.read(reinterpret_cast<char*>(destination.data()),
             static_cast<std::streamsize>(destination.size()));
  return input.good() ||
         (input.eof() && input.gcount() ==
                             static_cast<std::streamsize>(destination.size()));
}

bool WriteLittleU16(std::ostream& output, const std::uint16_t value) {
  const std::array<std::uint8_t, 2> bytes{
      static_cast<std::uint8_t>(value),
      static_cast<std::uint8_t>(value >> 8U),
  };
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return static_cast<bool>(output);
}

bool WriteLittleU32(std::ostream& output, const std::uint32_t value) {
  const std::array<std::uint8_t, 4> bytes{
      static_cast<std::uint8_t>(value),
      static_cast<std::uint8_t>(value >> 8U),
      static_cast<std::uint8_t>(value >> 16U),
      static_cast<std::uint8_t>(value >> 24U),
  };
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return static_cast<bool>(output);
}

bool WriteBytes(std::ostream& output,
                const std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return true;
  }
  if (bytes.size() > static_cast<std::size_t>(
                         std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

bool IsSupportedMethod(const std::uint16_t method) noexcept {
  return method == kZipMethodStored || method == kZipMethodDeflate;
}

bool ContainsNonAscii(const std::string_view value) noexcept {
  return std::any_of(value.begin(), value.end(), [](const unsigned char byte) {
    return byte >= 0x80U;
  });
}

bool IsSafeEntryName(const std::string_view name) noexcept {
  return !name.empty() && name != "." && name != ".." &&
         name.find('\0') == std::string_view::npos &&
         name.find('/') == std::string_view::npos &&
         name.find('\\') == std::string_view::npos &&
         name.find(':') == std::string_view::npos;
}

std::optional<std::filesystem::path> UniqueSiblingPath(
    const std::filesystem::path& path, const std::string_view suffix) {
  std::random_device random;
  for (unsigned attempt = 0; attempt != 32; ++attempt) {
    auto candidate = path;
    candidate += "." + std::to_string(random()) + std::string(suffix);
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) && !error) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::pair<std::uint16_t, std::uint16_t> CurrentDosTimestamp() noexcept {
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &now) != 0) {
    return {};
  }
#else
  if (localtime_r(&now, &local) == nullptr) {
    return {};
  }
#endif
  const int year = std::clamp(local.tm_year + 1900, 1980, 2107);
  const auto dos_time = static_cast<std::uint16_t>(
      (std::clamp(local.tm_hour, 0, 23) << 11U) |
      (std::clamp(local.tm_min, 0, 59) << 5U) |
      (std::clamp(local.tm_sec, 0, 59) / 2));
  const auto dos_date = static_cast<std::uint16_t>(
      ((year - 1980) << 9U) |
      ((std::clamp(local.tm_mon, 0, 11) + 1) << 5U) |
      std::clamp(local.tm_mday, 1, 31));
  return {dos_time, dos_date};
}

std::uint32_t ComputeCrc32(const std::span<const std::uint8_t> bytes) {
  uLong crc = crc32(0L, Z_NULL, 0);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count = static_cast<uInt>(std::min<std::size_t>(
        bytes.size() - offset, std::numeric_limits<uInt>::max()));
    crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes.data() + offset),
                count);
    offset += count;
  }
  return static_cast<std::uint32_t>(crc);
}

std::string_view ResourceKindName(const FrameReplayResourceKind kind) noexcept {
  switch (kind) {
    case FrameReplayResourceKind::kVertexFormat:
      return "VertexFormat";
    case FrameReplayResourceKind::kShader:
      return "Shader";
    case FrameReplayResourceKind::kBuffer:
      return "Buffer";
    case FrameReplayResourceKind::kTexture:
      return "Texture";
  }
  return "";
}

}

struct FrameReplayArchive::Impl {
  struct Entry {
    std::string name;
    std::uint16_t version_needed{kZipVersion20};
    std::uint16_t flags{0};
    std::uint16_t method{kZipMethodDeflate};
    std::uint16_t dos_time{0};
    std::uint16_t dos_date{0};
    std::uint32_t crc32{0};
    std::uint32_t compressed_size{0};
    std::uint32_t uncompressed_size{0};
    std::uint16_t internal_attributes{0};
    std::uint32_t external_attributes{0};
    std::uint32_t local_header_offset{0};
  };

  std::filesystem::path path;
  std::filesystem::path staging_path;
  FrameReplayArchiveMode mode{FrameReplayArchiveMode::kRead};
  FrameReplayArchiveLimits limits;
  std::vector<Entry> entries;
  std::map<std::string, std::size_t, std::less<>> lookup;
  std::fstream writer;
  std::uint64_t append_offset{0};
  bool open{true};
  bool failed{false};

  ~Impl() { RollBackFailedWrite(); }
  [[nodiscard]] bool LoadIndex(FrameReplayArchiveError* error);
  [[nodiscard]] bool ValidateLocalEntry(std::ifstream& input,
                                        const Entry& entry,
                                        std::uint64_t file_size,
                                        std::uint64_t data_limit,
                                        FrameReplayArchiveError* error) const;
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> Read(
      std::string_view name, FrameReplayArchiveError* error) const;
  [[nodiscard]] bool Write(std::string_view name,
                           std::span<const std::uint8_t> bytes,
                           FrameReplayArchiveError* error);
  [[nodiscard]] bool Finalize(FrameReplayArchiveError* error);
  [[nodiscard]] bool Publish(FrameReplayArchiveError* error);
  void RollBackFailedWrite() noexcept;
};

std::string FrameReplayBatchEntryName(const std::uint32_t batch) {
  std::array<char, 40> result{};
  const int count =
      std::snprintf(result.data(), result.size(), "Batch_%08u.frameReplay", batch);
  return count > 0 ? std::string(result.data(), static_cast<std::size_t>(count))
                   : std::string{};
}

std::string FrameReplayResourceEntryName(const FrameReplayResourceKind kind,
                                         const std::uint32_t identifier) {
  std::array<char, 64> result{};
  const auto prefix = ResourceKindName(kind);
  if (prefix.empty()) {
    return {};
  }
  const int count = std::snprintf(result.data(), result.size(), "%.*s_%08X.frameReplay",
                                  static_cast<int>(prefix.size()), prefix.data(),
                                  identifier);
  return count > 0 ? std::string(result.data(), static_cast<std::size_t>(count))
                   : std::string{};
}

bool FrameReplayArchive::Impl::ValidateLocalEntry(
    std::ifstream& input, const Entry& entry, const std::uint64_t file_size,
    const std::uint64_t data_limit,
    FrameReplayArchiveError* error) const {
  std::uint64_t fixed_end = 0;
  if (!CheckedAdd<std::uint64_t>(entry.local_header_offset,
                                 kLocalFileHeaderBytes, fixed_end) ||
      fixed_end > file_size) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local file header lies outside the archive");
  }

  std::array<std::uint8_t, kLocalFileHeaderBytes> header{};
  if (!ReadAt(input, entry.local_header_offset, header)) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to read local file header");
  }
  if (ReadLittleU32(header.data()) != kLocalFileHeaderSignature) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "invalid local file header signature");
  }
  const auto local_flags = ReadLittleU16(header.data() + 6);
  const auto local_method = ReadLittleU16(header.data() + 8);
  if (local_flags != entry.flags || local_method != entry.method) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local and central entry metadata disagree");
  }

  const std::uint16_t name_bytes = ReadLittleU16(header.data() + 26);
  const std::uint16_t extra_bytes = ReadLittleU16(header.data() + 28);
  std::uint64_t data_offset = fixed_end;
  if (!CheckedAdd<std::uint64_t>(data_offset, name_bytes, data_offset) ||
      !CheckedAdd<std::uint64_t>(data_offset, extra_bytes, data_offset)) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local entry metadata overflows its file offset");
  }
  std::uint64_t data_end = 0;
  if (!CheckedAdd<std::uint64_t>(data_offset, entry.compressed_size, data_end) ||
      data_end > file_size || data_end > data_limit) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "compressed entry data lies outside the archive");
  }

  if (name_bytes != entry.name.size()) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local and central entry names have different lengths");
  }
  if ((entry.flags & kDataDescriptorFlag) == 0 &&
      (ReadLittleU32(header.data() + 14) != entry.crc32 ||
       ReadLittleU32(header.data() + 18) != entry.compressed_size ||
       ReadLittleU32(header.data() + 22) != entry.uncompressed_size)) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local and central entry sizes or checksum disagree");
  }
  std::vector<std::uint8_t> local_name(name_bytes);
  if (!local_name.empty() &&
      !ReadAt(input, fixed_end, std::span<std::uint8_t>(local_name))) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to read local entry name");
  }
  if (!std::equal(local_name.begin(), local_name.end(), entry.name.begin(),
                  entry.name.end())) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "local and central entry names disagree");
  }
  return true;
}

bool FrameReplayArchive::Impl::LoadIndex(FrameReplayArchiveError* error) {
  std::error_code filesystem_error;
  const auto file_size = std::filesystem::file_size(path, filesystem_error);
  if (filesystem_error) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to determine archive size");
  }
  if (file_size > limits.max_archive_bytes) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "archive exceeds the configured byte limit");
  }
  if (file_size < kEndOfCentralDirectoryBytes) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "archive is too short for a ZIP end record");
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to open archive for reading");
  }

  const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(
      file_size, kEndOfCentralDirectoryBytes + kMaximumZipCommentBytes));
  const std::uint64_t tail_offset = file_size - tail_size;
  std::vector<std::uint8_t> tail(tail_size);
  if (!ReadAt(input, tail_offset, tail)) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to read ZIP end record search window");
  }

  std::optional<std::size_t> end_position;
  for (std::size_t position = tail.size() - kEndOfCentralDirectoryBytes;;
       --position) {
    if (ReadLittleU32(tail.data() + position) ==
        kEndOfCentralDirectorySignature) {
      const auto comment_bytes = ReadLittleU16(tail.data() + position + 20);
      if (position + kEndOfCentralDirectoryBytes + comment_bytes ==
          tail.size()) {
        end_position = position;
        break;
      }
    }
    if (position == 0) {
      break;
    }
  }
  if (!end_position.has_value()) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "ZIP end record was not found");
  }

  const auto* end = tail.data() + *end_position;
  const auto disk = ReadLittleU16(end + 4);
  const auto central_disk = ReadLittleU16(end + 6);
  const auto disk_entries = ReadLittleU16(end + 8);
  const auto total_entries = ReadLittleU16(end + 10);
  const std::uint32_t central_size = ReadLittleU32(end + 12);
  const std::uint32_t central_offset = ReadLittleU32(end + 16);
  if (disk != 0 || central_disk != 0 || disk_entries != total_entries) {
    return Fail(error, FrameReplayArchiveErrorCode::kUnsupportedArchive,
                "multi-disk ZIP archives are not supported");
  }
  if (total_entries > limits.max_entries) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "archive contains too many entries");
  }
  const std::uint64_t end_offset = tail_offset + *end_position;
  std::uint64_t central_end = 0;
  if (!CheckedAdd<std::uint64_t>(central_offset, central_size, central_end) ||
      central_end != end_offset) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "central directory lies outside the archive");
  }
  append_offset = central_offset;

  entries.clear();
  lookup.clear();
  entries.reserve(total_entries);
  std::uint64_t cursor = central_offset;
  for (std::size_t index = 0; index < total_entries; ++index) {
    std::uint64_t fixed_end = 0;
    if (!CheckedAdd<std::uint64_t>(cursor, kCentralDirectoryHeaderBytes,
                                   fixed_end) ||
        fixed_end > central_end) {
      return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                  "central directory entry is truncated");
    }
    std::array<std::uint8_t, kCentralDirectoryHeaderBytes> header{};
    if (!ReadAt(input, cursor, header)) {
      return Fail(error, FrameReplayArchiveErrorCode::kIo,
                  "failed to read central directory entry");
    }
    if (ReadLittleU32(header.data()) != kCentralDirectorySignature) {
      return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                  "invalid central directory signature");
    }

    Entry entry;
    entry.version_needed = ReadLittleU16(header.data() + 6);
    entry.flags = ReadLittleU16(header.data() + 8);
    entry.method = ReadLittleU16(header.data() + 10);
    entry.dos_time = ReadLittleU16(header.data() + 12);
    entry.dos_date = ReadLittleU16(header.data() + 14);
    entry.crc32 = ReadLittleU32(header.data() + 16);
    entry.compressed_size = ReadLittleU32(header.data() + 20);
    entry.uncompressed_size = ReadLittleU32(header.data() + 24);
    const std::uint16_t name_bytes = ReadLittleU16(header.data() + 28);
    const std::uint16_t extra_bytes = ReadLittleU16(header.data() + 30);
    const std::uint16_t comment_bytes = ReadLittleU16(header.data() + 32);
    const std::uint16_t start_disk = ReadLittleU16(header.data() + 34);
    entry.internal_attributes = ReadLittleU16(header.data() + 36);
    entry.external_attributes = ReadLittleU32(header.data() + 38);
    entry.local_header_offset = ReadLittleU32(header.data() + 42);

    if ((entry.flags & ~kSupportedFlags) != 0) {
      return Fail(error, FrameReplayArchiveErrorCode::kUnsupportedArchive,
                   "frame replay entry uses unsupported ZIP flags");
    }
    if (!IsSupportedMethod(entry.method)) {
      return Fail(error, FrameReplayArchiveErrorCode::kUnsupportedArchive,
                  "frame replay entry uses an unsupported ZIP method");
    }
    const auto minimum_version = entry.method == kZipMethodStored
                                     ? kZipVersion10
                                     : kZipVersion20;
    if (entry.version_needed < minimum_version ||
        (entry.method == kZipMethodStored &&
         (entry.flags & kCompressionOptionFlags) != 0)) {
      return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                  "entry ZIP version or method flags are inconsistent");
    }
    if (start_disk != 0 || entry.version_needed > kZipVersion20 ||
        entry.compressed_size == 0xFFFFFFFFU ||
        entry.uncompressed_size == 0xFFFFFFFFU ||
        entry.local_header_offset == 0xFFFFFFFFU) {
      return Fail(error, FrameReplayArchiveErrorCode::kUnsupportedArchive,
                  "entry requires unsupported ZIP features");
    }
    if (name_bytes > limits.max_entry_name_bytes) {
      return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                  "entry name exceeds the configured byte limit");
    }

    std::uint64_t variable_bytes = name_bytes;
    if (!CheckedAdd<std::uint64_t>(variable_bytes, extra_bytes,
                                   variable_bytes) ||
        !CheckedAdd<std::uint64_t>(variable_bytes, comment_bytes,
                                   variable_bytes) ||
        !CheckedAdd<std::uint64_t>(fixed_end, variable_bytes, cursor) ||
        cursor > central_end) {
      return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                  "central directory variable fields are truncated");
    }
    std::vector<std::uint8_t> name(name_bytes);
    if (!name.empty() &&
        !ReadAt(input, fixed_end, std::span<std::uint8_t>(name))) {
      return Fail(error, FrameReplayArchiveErrorCode::kIo,
                  "failed to read central directory entry name");
    }
    if (std::find(name.begin(), name.end(), std::uint8_t{0}) != name.end()) {
      return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                  "entry name contains an embedded NUL byte");
    }
    if (!name.empty()) {
      entry.name.assign(reinterpret_cast<const char*>(name.data()), name.size());
    }
    if (!IsSafeEntryName(entry.name)) {
      return Fail(error, FrameReplayArchiveErrorCode::kInvalidPath,
                  "entry name is not a safe flat archive member");
    }
    if (!ValidateLocalEntry(input, entry, file_size, central_offset, error)) {
      return false;
    }

    entries.push_back(std::move(entry));
    lookup[entries.back().name] = entries.size() - 1;
  }
  if (cursor != central_end) {
    return Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
                "central directory size does not match its entries");
  }
  return true;
}

std::optional<std::vector<std::uint8_t>> FrameReplayArchive::Impl::Read(
    const std::string_view name, FrameReplayArchiveError* error) const {
  ClearError(error);
  if (!open) {
    Fail(error, FrameReplayArchiveErrorCode::kClosed,
         "frame replay archive is closed");
    return std::nullopt;
  }
  if (mode != FrameReplayArchiveMode::kRead) {
    Fail(error, FrameReplayArchiveErrorCode::kInvalidOperation,
         "entries can only be read from a read-mode archive");
    return std::nullopt;
  }
  const auto found = lookup.find(name);
  if (found == lookup.end()) {
    Fail(error, FrameReplayArchiveErrorCode::kEntryNotFound,
         "frame replay entry was not found");
    return std::nullopt;
  }
  const Entry& entry = entries[found->second];
  if (entry.uncompressed_size > limits.max_entry_uncompressed_bytes ||
      !std::in_range<std::size_t>(entry.uncompressed_size)) {
    Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
         "entry exceeds the configured allocation limit");
    return std::nullopt;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    Fail(error, FrameReplayArchiveErrorCode::kIo,
         "failed to open archive for entry reading");
    return std::nullopt;
  }
  std::array<std::uint8_t, kLocalFileHeaderBytes> local{};
  if (!ReadAt(input, entry.local_header_offset, local)) {
    Fail(error, FrameReplayArchiveErrorCode::kIo,
         "failed to read local file header");
    return std::nullopt;
  }
  std::uint64_t data_offset = entry.local_header_offset + local.size();
  data_offset += ReadLittleU16(local.data() + 26);
  data_offset += ReadLittleU16(local.data() + 28);

  std::vector<std::uint8_t> result(entry.uncompressed_size);
  if (entry.method == kZipMethodStored) {
    if (entry.compressed_size != entry.uncompressed_size ||
        (!result.empty() && !ReadAt(input, data_offset, result))) {
      Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
           "stored entry has inconsistent lengths or truncated data");
      return std::nullopt;
    }
  } else {
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
      Fail(error, FrameReplayArchiveErrorCode::kCompression,
           "failed to initialize raw ZIP inflater");
      return std::nullopt;
    }

    std::array<std::uint8_t, kIoBufferBytes> input_buffer{};
    std::array<std::uint8_t, 1> overflow_sink{};
    std::uint64_t remaining_compressed = entry.compressed_size;
    std::size_t output_offset = 0;
    int status = Z_OK;
    bool stream_failed = false;
    while (status != Z_STREAM_END) {
      if (stream.avail_in == 0 && remaining_compressed != 0) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            remaining_compressed, input_buffer.size()));
        const std::uint64_t consumed =
            entry.compressed_size - remaining_compressed;
        if (!ReadAt(input, data_offset + consumed,
                    std::span<std::uint8_t>(input_buffer.data(), count))) {
          stream_failed = true;
          break;
        }
        stream.next_in = input_buffer.data();
        stream.avail_in = static_cast<uInt>(count);
        remaining_compressed -= count;
      }

      const bool output_is_full = output_offset == result.size();
      const auto output_count = output_is_full
                                    ? overflow_sink.size()
                                    : std::min<std::size_t>(
                                          result.size() - output_offset,
                                          std::numeric_limits<uInt>::max());
      stream.next_out = output_is_full ? overflow_sink.data()
                                       : result.data() + output_offset;
      stream.avail_out = static_cast<uInt>(output_count);
      const auto before = stream.avail_out;
      status = inflate(&stream, Z_NO_FLUSH);
      const std::size_t produced = before - stream.avail_out;
      if (output_is_full && produced != 0) {
        stream_failed = true;
        break;
      }
      output_offset += produced;
      if (status != Z_OK && status != Z_STREAM_END) {
        stream_failed = true;
        break;
      }
      if (status == Z_OK && stream.avail_in == 0 &&
          remaining_compressed == 0 && produced == 0) {
        stream_failed = true;
        break;
      }
    }
    const bool consumed_exactly =
        remaining_compressed == 0 && stream.avail_in == 0;
    inflateEnd(&stream);
    if (stream_failed || status != Z_STREAM_END || !consumed_exactly ||
        output_offset != result.size()) {
      Fail(error, FrameReplayArchiveErrorCode::kMalformedArchive,
           "deflated entry has invalid or inconsistent data");
      return std::nullopt;
    }
  }

  if (ComputeCrc32(result) != entry.crc32) {
    Fail(error, FrameReplayArchiveErrorCode::kChecksumMismatch,
         "frame replay entry CRC32 does not match");
    return std::nullopt;
  }
  return result;
}

bool FrameReplayArchive::Impl::Write(
    const std::string_view name, const std::span<const std::uint8_t> bytes,
    FrameReplayArchiveError* error) {
  ClearError(error);
  if (!open) {
    return Fail(error, FrameReplayArchiveErrorCode::kClosed,
                "frame replay archive is closed");
  }
  if (mode == FrameReplayArchiveMode::kRead) {
    return Fail(error, FrameReplayArchiveErrorCode::kInvalidOperation,
                "entries cannot be written to a read-mode archive");
  }
  if (failed || !writer) {
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "archive writer is not usable");
  }
  if (name.size() > limits.max_entry_name_bytes ||
      name.size() > std::numeric_limits<std::uint16_t>::max()) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "entry name exceeds the configured byte limit");
  }
  if (!IsSafeEntryName(name)) {
    return Fail(error, FrameReplayArchiveErrorCode::kInvalidPath,
                "entry name is not a safe flat archive member");
  }
  if (bytes.size() > limits.max_entry_uncompressed_bytes ||
      bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "entry exceeds the configured ZIP32 byte limit");
  }
  if (entries.size() >= limits.max_entries ||
      entries.size() >= std::numeric_limits<std::uint16_t>::max()) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "archive contains too many entries");
  }

  const auto position = writer.tellp();
  const auto position_offset = static_cast<std::streamoff>(position);
  if (position_offset < 0 ||
      static_cast<std::uint64_t>(position_offset) >
          std::numeric_limits<std::uint32_t>::max()) {
    failed = true;
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "archive offset exceeds ZIP32 limits");
  }
  std::uint64_t local_end = static_cast<std::uint64_t>(position_offset);
  if (!CheckedAdd<std::uint64_t>(local_end, kLocalFileHeaderBytes, local_end) ||
      !CheckedAdd<std::uint64_t>(local_end, name.size(), local_end) ||
      local_end > limits.max_archive_bytes) {
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "local entry header exceeds archive byte limit");
  }

  Entry entry;
  entry.name = std::string(name);
  entry.flags = ContainsNonAscii(name) ? kUtf8Flag : 0;
  entry.method = kZipMethodDeflate;
  const auto [dos_time, dos_date] = CurrentDosTimestamp();
  entry.dos_time = dos_time;
  entry.dos_date = dos_date;
  entry.crc32 = ComputeCrc32(bytes);
  entry.uncompressed_size = static_cast<std::uint32_t>(bytes.size());
  entry.local_header_offset = static_cast<std::uint32_t>(position_offset);

  const auto write_local_header = [&]() {
    return WriteLittleU32(writer, kLocalFileHeaderSignature) &&
           WriteLittleU16(writer, kZipVersion20) &&
           WriteLittleU16(writer, entry.flags) &&
           WriteLittleU16(writer, entry.method) &&
           WriteLittleU16(writer, entry.dos_time) &&
           WriteLittleU16(writer, entry.dos_date) &&
           WriteLittleU32(writer, entry.crc32) &&
           WriteLittleU32(writer, 0) &&
           WriteLittleU32(writer, entry.uncompressed_size) &&
           WriteLittleU16(writer,
                          static_cast<std::uint16_t>(entry.name.size())) &&
           WriteLittleU16(writer, 0) &&
           WriteBytes(writer,
                      std::span<const std::uint8_t>(
                          reinterpret_cast<const std::uint8_t*>(entry.name.data()),
                          entry.name.size()));
  };
  if (!write_local_header()) {
    failed = true;
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to write local file header");
  }

  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    failed = true;
    return Fail(error, FrameReplayArchiveErrorCode::kCompression,
                "failed to initialize raw ZIP deflater");
  }

  std::array<std::uint8_t, kIoBufferBytes> output_buffer{};
  std::size_t input_offset = 0;
  std::uint64_t compressed_size = 0;
  int status = Z_OK;
  bool compression_failed = false;
  bool limit_exceeded = false;
  while (status != Z_STREAM_END) {
    if (stream.avail_in == 0 && input_offset < bytes.size()) {
      const auto count = static_cast<uInt>(std::min<std::size_t>(
          bytes.size() - input_offset, std::numeric_limits<uInt>::max()));
      stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(
          bytes.data() + input_offset));
      stream.avail_in = count;
      input_offset += count;
    }
    const int flush =
        input_offset == bytes.size() && stream.avail_in == 0 ? Z_FINISH
                                                             : Z_NO_FLUSH;
    do {
      stream.next_out = output_buffer.data();
      stream.avail_out = static_cast<uInt>(output_buffer.size());
      status = deflate(&stream, flush);
      if (status != Z_OK && status != Z_STREAM_END) {
        compression_failed = true;
        break;
      }
      const std::size_t produced = output_buffer.size() - stream.avail_out;
      if (compressed_size > std::numeric_limits<std::uint32_t>::max() -
                                 produced ||
          compressed_size + produced > limits.max_archive_bytes - local_end ||
          !WriteBytes(writer, std::span<const std::uint8_t>(
                                  output_buffer.data(), produced))) {
        limit_exceeded =
            compressed_size > std::numeric_limits<std::uint32_t>::max() -
                                  produced ||
            compressed_size + produced > limits.max_archive_bytes - local_end;
        compression_failed = true;
        break;
      }
      compressed_size += produced;
    } while (status != Z_STREAM_END && stream.avail_out == 0);
    if (compression_failed) {
      break;
    }
  }
  deflateEnd(&stream);
  if (compression_failed || status != Z_STREAM_END) {
    failed = true;
    return Fail(error, limit_exceeded
                           ? FrameReplayArchiveErrorCode::kLimitExceeded
                           : FrameReplayArchiveErrorCode::kCompression,
                limit_exceeded
                    ? "compressed entry exceeds archive byte limit"
                    : "failed while deflating frame replay entry");
  }
  entry.compressed_size = static_cast<std::uint32_t>(compressed_size);

  const auto entry_end = writer.tellp();
  writer.seekp(static_cast<std::streamoff>(entry.local_header_offset) + 18,
               std::ios::beg);
  if (!WriteLittleU32(writer, entry.compressed_size)) {
    failed = true;
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to patch compressed entry size");
  }
  writer.seekp(entry_end);
  const auto entry_end_offset = static_cast<std::streamoff>(entry_end);
  if (!writer || entry_end_offset < 0 ||
      static_cast<std::uint64_t>(entry_end_offset) >
          limits.max_archive_bytes) {
    failed = true;
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "archive exceeds the configured ZIP32 byte limit");
  }

  entries.push_back(std::move(entry));
  lookup[entries.back().name] = entries.size() - 1;
  return true;
}

bool FrameReplayArchive::Impl::Finalize(FrameReplayArchiveError* error) {
  ClearError(error);
  if (!open) {
    return true;
  }
  if (mode == FrameReplayArchiveMode::kRead) {
    open = false;
    return true;
  }
  if (failed || !writer) {
    RollBackFailedWrite();
    open = false;
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "archive cannot be finalized after an earlier write failure");
  }

  const auto central_position = writer.tellp();
  const auto central_position_offset =
      static_cast<std::streamoff>(central_position);
  if (central_position_offset < 0 ||
      static_cast<std::uint64_t>(central_position_offset) >
          std::numeric_limits<std::uint32_t>::max() ||
      entries.size() > std::numeric_limits<std::uint16_t>::max()) {
    RollBackFailedWrite();
    open = false;
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "central directory exceeds ZIP32 limits");
  }
  const auto central_offset =
      static_cast<std::uint32_t>(central_position_offset);

  std::uint64_t final_bound = central_offset;
  for (const auto& entry : entries) {
    if (!CheckedAdd<std::uint64_t>(final_bound,
                                   kCentralDirectoryHeaderBytes,
                                   final_bound) ||
        !CheckedAdd<std::uint64_t>(final_bound, entry.name.size(),
                                   final_bound)) {
      RollBackFailedWrite();
      open = false;
      return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                  "central directory byte count overflows");
    }
  }
  if (!CheckedAdd<std::uint64_t>(final_bound, kEndOfCentralDirectoryBytes,
                                 final_bound) ||
      final_bound > limits.max_archive_bytes ||
      final_bound > std::numeric_limits<std::uint32_t>::max()) {
    RollBackFailedWrite();
    open = false;
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "final archive exceeds configured ZIP32 byte limit");
  }

  std::sort(entries.begin(), entries.end(),
            [](const Entry& lhs, const Entry& rhs) {
              return lhs.name != rhs.name ? lhs.name < rhs.name
                                          : lhs.local_header_offset <
                                                rhs.local_header_offset;
            });
  for (const auto& entry : entries) {
    const bool wrote =
        WriteLittleU32(writer, kCentralDirectorySignature) &&
        WriteLittleU16(writer, kZipVersion20) &&
        WriteLittleU16(writer, entry.version_needed) &&
        WriteLittleU16(writer, entry.flags) &&
        WriteLittleU16(writer, entry.method) &&
        WriteLittleU16(writer, entry.dos_time) &&
        WriteLittleU16(writer, entry.dos_date) &&
        WriteLittleU32(writer, entry.crc32) &&
        WriteLittleU32(writer, entry.compressed_size) &&
        WriteLittleU32(writer, entry.uncompressed_size) &&
        WriteLittleU16(writer,
                       static_cast<std::uint16_t>(entry.name.size())) &&
        WriteLittleU16(writer, 0) && WriteLittleU16(writer, 0) &&
        WriteLittleU16(writer, 0) &&
        WriteLittleU16(writer, entry.internal_attributes) &&
        WriteLittleU32(writer, entry.external_attributes) &&
        WriteLittleU32(writer, entry.local_header_offset) &&
        WriteBytes(writer,
                   std::span<const std::uint8_t>(
                       reinterpret_cast<const std::uint8_t*>(entry.name.data()),
                       entry.name.size()));
    if (!wrote) {
      RollBackFailedWrite();
      open = false;
      return Fail(error, FrameReplayArchiveErrorCode::kIo,
                  "failed to write central directory entry");
    }
  }

  const auto central_end = writer.tellp();
  const auto central_end_offset = static_cast<std::streamoff>(central_end);
  if (central_end_offset < central_position_offset ||
      static_cast<std::uint64_t>(central_end_offset -
                                 central_position_offset) >
          std::numeric_limits<std::uint32_t>::max()) {
    RollBackFailedWrite();
    open = false;
    return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
                "central directory size exceeds ZIP32 limits");
  }
  const auto central_size =
      static_cast<std::uint32_t>(central_end_offset -
                                 central_position_offset);
  const auto entry_count = static_cast<std::uint16_t>(entries.size());
  const bool wrote_end =
      WriteLittleU32(writer, kEndOfCentralDirectorySignature) &&
      WriteLittleU16(writer, 0) && WriteLittleU16(writer, 0) &&
      WriteLittleU16(writer, entry_count) &&
      WriteLittleU16(writer, entry_count) &&
      WriteLittleU32(writer, central_size) &&
      WriteLittleU32(writer, central_offset) && WriteLittleU16(writer, 0);
  writer.flush();
  const auto final_offset = static_cast<std::streamoff>(writer.tellp());
  bool complete =
      wrote_end && static_cast<bool>(writer) && final_offset >= 0 &&
      static_cast<std::uint64_t>(final_offset) <= limits.max_archive_bytes;
  writer.close();
  complete = complete && !writer.fail();
  if (complete) {
    std::error_code resize_error;
    std::filesystem::resize_file(staging_path,
                                 static_cast<std::uint64_t>(final_offset),
                                 resize_error);
    complete = !resize_error;
  }
  open = false;
  if (!complete) {
    RollBackFailedWrite();
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to finalize ZIP end record");
  }
  return Publish(error);
}

bool FrameReplayArchive::Impl::Publish(FrameReplayArchiveError* error) {
  std::error_code publish_error;
  std::filesystem::rename(staging_path, path, publish_error);
  if (!publish_error) {
    staging_path.clear();
    return true;
  }

  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error) || exists_error) {
    RollBackFailedWrite();
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to publish frame replay archive");
  }
  const auto backup = UniqueSiblingPath(path, ".backup");
  if (!backup.has_value()) {
    RollBackFailedWrite();
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to reserve archive rollback path");
  }
  std::filesystem::rename(path, *backup, publish_error);
  if (publish_error) {
    RollBackFailedWrite();
    return Fail(error, FrameReplayArchiveErrorCode::kIo,
                "failed to preserve previous frame replay archive");
  }
  std::filesystem::rename(staging_path, path, publish_error);
  if (publish_error) {
    std::error_code rollback_error;
    std::filesystem::rename(*backup, path, rollback_error);
    RollBackFailedWrite();
    return Fail(error,
                rollback_error ? FrameReplayArchiveErrorCode::kRollbackFailed
                               : FrameReplayArchiveErrorCode::kIo,
                rollback_error ? "archive publish and rollback both failed"
                               : "failed to publish frame replay archive");
  }
  staging_path.clear();
  std::error_code ignored;
  std::filesystem::remove(*backup, ignored);
  return true;
}

void FrameReplayArchive::Impl::RollBackFailedWrite() noexcept {
  writer.close();
  std::error_code ignored;
  if (!staging_path.empty()) {
    std::filesystem::remove(staging_path, ignored);
  }
}

std::optional<FrameReplayArchive> FrameReplayArchive::Open(
    const std::filesystem::path& path, const FrameReplayArchiveMode mode,
    FrameReplayArchiveError* error, const FrameReplayArchiveLimits limits) try {
  ClearError(error);
  if (limits.max_entries > std::numeric_limits<std::uint16_t>::max() ||
      limits.max_entry_name_bytes >
          std::numeric_limits<std::uint16_t>::max() ||
      limits.max_archive_bytes > std::numeric_limits<std::uint32_t>::max()) {
    Fail(error, FrameReplayArchiveErrorCode::kInvalidOperation,
         "archive limits exceed ZIP32 representation limits");
    return std::nullopt;
  }

  auto impl = std::make_unique<Impl>();
  impl->path = path;
  impl->mode = mode;
  impl->limits = limits;

  if (path.empty() || path.filename().empty()) {
    Fail(error, FrameReplayArchiveErrorCode::kInvalidPath,
         "archive path must name a file");
    return std::nullopt;
  }

  switch (mode) {
    case FrameReplayArchiveMode::kCreate: {
      std::error_code directory_error;
      const auto parent = path.parent_path();
      if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
      }
      if (directory_error) {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to create frame replay archive directory");
        return std::nullopt;
      }
      const auto staging = UniqueSiblingPath(path, ".staging");
      if (!staging.has_value()) {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to reserve archive staging path");
        return std::nullopt;
      }
      impl->staging_path = *staging;
      impl->writer.open(impl->staging_path,
                        std::ios::binary | std::ios::in | std::ios::out |
                            std::ios::trunc);
      if (!impl->writer) {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to create frame replay archive");
        return std::nullopt;
      }
      break;
    }
    case FrameReplayArchiveMode::kAppend:
      if (!impl->LoadIndex(error)) {
        return std::nullopt;
      }
      if (const auto staging = UniqueSiblingPath(path, ".staging")) {
        impl->staging_path = *staging;
      } else {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to reserve archive staging path");
        return std::nullopt;
      }
      {
        std::error_code copy_error;
        std::filesystem::copy_file(path, impl->staging_path,
                                   std::filesystem::copy_options::none,
                                   copy_error);
        if (copy_error) {
          Fail(error, FrameReplayArchiveErrorCode::kIo,
               "failed to stage frame replay archive for append");
          return std::nullopt;
        }
      }
      impl->writer.open(impl->staging_path,
                         std::ios::binary | std::ios::in | std::ios::out);
      if (!impl->writer) {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to open frame replay archive for append");
        return std::nullopt;
      }
      impl->writer.seekp(static_cast<std::streamoff>(impl->append_offset),
                         std::ios::beg);
      if (!impl->writer) {
        Fail(error, FrameReplayArchiveErrorCode::kIo,
             "failed to seek to frame replay archive end");
        return std::nullopt;
      }
      break;
    case FrameReplayArchiveMode::kRead:
      if (!impl->LoadIndex(error)) {
        return std::nullopt;
      }
      break;
    default:
      Fail(error, FrameReplayArchiveErrorCode::kInvalidOperation,
           "unknown frame replay archive mode");
      return std::nullopt;
  }
  return FrameReplayArchive(std::move(impl));
} catch (const std::bad_alloc&) {
  Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
       "memory allocation failed while opening archive");
  return std::nullopt;
} catch (const std::filesystem::filesystem_error&) {
  Fail(error, FrameReplayArchiveErrorCode::kIo,
       "filesystem operation failed while opening archive");
  return std::nullopt;
} catch (...) {
  Fail(error, FrameReplayArchiveErrorCode::kIo,
       "unexpected failure while opening archive");
  return std::nullopt;
}

FrameReplayArchive::FrameReplayArchive(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

FrameReplayArchive::~FrameReplayArchive() {
  if (impl_ != nullptr) {
    try {
      (void)impl_->Finalize(nullptr);
    } catch (...) {
      impl_->RollBackFailedWrite();
    }
  }
}

FrameReplayArchive::FrameReplayArchive(FrameReplayArchive&&) noexcept = default;

FrameReplayArchive& FrameReplayArchive::operator=(
    FrameReplayArchive&& other) noexcept {
  if (this != &other) {
    if (impl_ != nullptr) {
      try {
        (void)impl_->Finalize(nullptr);
      } catch (...) {
        impl_->RollBackFailedWrite();
      }
    }
    impl_ = std::move(other.impl_);
  }
  return *this;
}

bool FrameReplayArchive::is_open() const noexcept {
  return impl_ != nullptr && impl_->open;
}

FrameReplayArchiveMode FrameReplayArchive::mode() const noexcept {
  return impl_ != nullptr ? impl_->mode : FrameReplayArchiveMode::kRead;
}

const std::filesystem::path& FrameReplayArchive::path() const noexcept {
  static const std::filesystem::path empty;
  return impl_ != nullptr ? impl_->path : empty;
}

std::vector<std::string> FrameReplayArchive::EntryNames() const {
  std::vector<std::string> result;
  if (impl_ == nullptr || !impl_->open) {
    return result;
  }
  result.reserve(impl_->lookup.size());
  for (const auto& [name, unused] : impl_->lookup) {
    (void)unused;
    result.push_back(name);
  }
  return result;
}

bool FrameReplayArchive::Contains(const std::string_view name) const noexcept {
  return impl_ != nullptr && impl_->open && impl_->lookup.contains(name);
}

std::optional<std::vector<std::uint8_t>> FrameReplayArchive::ReadEntry(
    const std::string_view name, FrameReplayArchiveError* error) const try {
  if (impl_ == nullptr) {
    Fail(error, FrameReplayArchiveErrorCode::kClosed,
         "frame replay archive is closed");
    return std::nullopt;
  }
  return impl_->Read(name, error);
} catch (const std::bad_alloc&) {
  Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
       "memory allocation failed while reading entry");
  return std::nullopt;
} catch (...) {
  Fail(error, FrameReplayArchiveErrorCode::kIo,
       "unexpected failure while reading entry");
  return std::nullopt;
}

bool FrameReplayArchive::WriteEntry(
    const std::string_view name, const std::span<const std::uint8_t> bytes,
    FrameReplayArchiveError* error) try {
  if (impl_ == nullptr) {
    return Fail(error, FrameReplayArchiveErrorCode::kClosed,
                "frame replay archive is closed");
  }
  return impl_->Write(name, bytes, error);
} catch (const std::bad_alloc&) {
  return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
              "memory allocation failed while writing entry");
} catch (...) {
  return Fail(error, FrameReplayArchiveErrorCode::kIo,
              "unexpected failure while writing entry");
}

bool FrameReplayArchive::Close(FrameReplayArchiveError* error) try {
  if (impl_ == nullptr) {
    ClearError(error);
    return true;
  }
  return impl_->Finalize(error);
} catch (const std::bad_alloc&) {
  if (impl_ != nullptr) {
    impl_->RollBackFailedWrite();
    impl_->open = false;
  }
  return Fail(error, FrameReplayArchiveErrorCode::kLimitExceeded,
              "memory allocation failed while finalizing archive");
} catch (...) {
  if (impl_ != nullptr) {
    impl_->RollBackFailedWrite();
    impl_->open = false;
  }
  return Fail(error, FrameReplayArchiveErrorCode::kIo,
              "unexpected failure while finalizing archive");
}

}

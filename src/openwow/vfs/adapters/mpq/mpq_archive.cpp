#include "openwow/vfs/adapters/mpq/mpq_archive.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

namespace openwow::vfs::mpq {

namespace {

constexpr std::uint32_t kMpqHeaderMagic = 0x1A51504Du;
constexpr std::uint32_t kMpqUserDataMagic = 0x1B51504Du;
constexpr std::uint32_t kMpqHeaderProbeWindowSize = 4096u;
constexpr std::uint32_t kMpqHeaderProbeStride = 512u;
constexpr std::uint32_t kMpqHeaderProbeMinimumSize = 44u;
constexpr std::uint32_t kMpqHeaderSizeV0 = 32u;
constexpr std::uint32_t kMpqHeaderSizeV1 = 44u;

struct ParsedArchiveHeader {
  std::uint32_t header_size = 0;
  std::uint32_t archive_size = 0;
  std::uint16_t format_version = 0;
  std::uint16_t sector_size_shift = 0;
  std::uint64_t hash_table_offset = 0;
  std::uint64_t block_table_offset = 0;
  std::uint64_t extended_block_table_offset = 0;
  std::uint32_t hash_table_entry_count = 0;
  std::uint32_t block_table_entry_count = 0;
  std::uint64_t table_region_end = 0;
};

std::uint16_t LoadLittleEndian16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>(bytes[0])
         | (static_cast<std::uint16_t>(bytes[1]) << 8);
}

std::uint32_t LoadLittleEndian32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0])
         | (static_cast<std::uint32_t>(bytes[1]) << 8)
         | (static_cast<std::uint32_t>(bytes[2]) << 16)
         | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint64_t ComposeOffset64(const std::uint32_t low, const std::uint32_t high) {
  return static_cast<std::uint64_t>(low)
         | (static_cast<std::uint64_t>(high) << 32);
}

bool SeekArchiveProbeStream(std::ifstream& input, const std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return false;
  }

  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  return input.good();
}

bool ReadArchiveProbeWindow(std::ifstream& input,
                            const std::uint64_t offset,
                            std::array<std::uint8_t, kMpqHeaderProbeWindowSize>& buffer,
                            std::size_t& out_size) {
  out_size = 0;
  if (!SeekArchiveProbeStream(input, offset)) {
    return false;
  }

  input.read(reinterpret_cast<char*>(buffer.data()),
             static_cast<std::streamsize>(buffer.size()));
  out_size = static_cast<std::size_t>(input.gcount());
  return input.eof() || input.good();
}

bool ParseArchiveHeaderCandidate(const std::uint8_t* bytes,
                                 const std::uint64_t file_size,
                                 ParsedArchiveHeader& out_header) {
  const std::uint32_t header_size = LoadLittleEndian32(bytes + 4);
  const std::uint32_t archive_size = LoadLittleEndian32(bytes + 8);
  const std::uint16_t format_version = LoadLittleEndian16(bytes + 12);
  const std::uint16_t sector_size_shift = LoadLittleEndian16(bytes + 14);
  const std::uint32_t hash_table_offset_low = LoadLittleEndian32(bytes + 16);
  const std::uint32_t block_table_offset_low = LoadLittleEndian32(bytes + 20);
  const std::uint32_t hash_table_entry_count = LoadLittleEndian32(bytes + 24);
  const std::uint32_t block_table_entry_count = LoadLittleEndian32(bytes + 28);

  std::uint32_t extended_block_table_offset_low = 0;
  std::uint32_t extended_block_table_offset_high = 0;
  std::uint16_t hash_table_offset_high = 0;
  std::uint16_t block_table_offset_high = 0;

  const bool valid_header_size =
      (format_version == 0 && header_size == kMpqHeaderSizeV0)
      || (format_version == 1 && header_size == kMpqHeaderSizeV1)
      || (format_version > 1 && header_size >= kMpqHeaderSizeV1);
  if (!valid_header_size) {
    return false;
  }

  if (format_version != 0) {
    extended_block_table_offset_low = LoadLittleEndian32(bytes + 32);
    extended_block_table_offset_high = LoadLittleEndian32(bytes + 36);
    hash_table_offset_high = LoadLittleEndian16(bytes + 40);
    block_table_offset_high = LoadLittleEndian16(bytes + 42);
  }

  const std::uint64_t hash_table_offset =
      ComposeOffset64(hash_table_offset_low, hash_table_offset_high);
  const std::uint64_t block_table_offset =
      ComposeOffset64(block_table_offset_low, block_table_offset_high);
  const std::uint64_t extended_block_table_offset =
      ComposeOffset64(extended_block_table_offset_low,
                      extended_block_table_offset_high);

  const std::uint64_t hash_table_end =
      hash_table_offset + 16ull * hash_table_entry_count;
  const std::uint64_t block_table_end =
      block_table_offset + 16ull * block_table_entry_count;
  const std::uint64_t extended_block_table_end =
      extended_block_table_offset + 2ull * block_table_entry_count;
  const std::uint64_t table_region_end =
      std::min(file_size,
               std::max(hash_table_end,
                        std::max(block_table_end, extended_block_table_end)));

  if (hash_table_offset > table_region_end || block_table_offset > table_region_end) {
    return false;
  }

  out_header = ParsedArchiveHeader{
      .header_size = header_size,
      .archive_size = archive_size,
      .format_version = format_version,
      .sector_size_shift = sector_size_shift,
      .hash_table_offset = hash_table_offset,
      .block_table_offset = block_table_offset,
      .extended_block_table_offset = extended_block_table_offset,
      .hash_table_entry_count = hash_table_entry_count,
      .block_table_entry_count = block_table_entry_count,
      .table_region_end = table_region_end,
  };
  return true;
}

std::string ToBackslashPath(std::string value) {
  std::replace(value.begin(), value.end(), '/', '\\');
  return value;
}

std::string NormalizeMpqVirtualPath(std::string virtual_path) {
  if (virtual_path.empty()) {
    return {};
  }

  std::replace(virtual_path.begin(), virtual_path.end(), '\\', '/');
  while (!virtual_path.empty() && virtual_path.front() == '/') {
    virtual_path.erase(virtual_path.begin());
  }
  while (virtual_path.find("//") != std::string::npos) {
    virtual_path = virtual_path.replace(virtual_path.find("//"), 2, "/");
  }
  return virtual_path;
}

std::string ToLowerAsciiCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
constexpr std::array<std::string_view, 2> kRebuildSyntheticEntries = {
    "(listfile)",
    "(attributes)",
};

constexpr std::array<std::string_view, 3> kTrackedInternalEntries = {
    "(listfile)",
    "(attributes)",
    "(signature)",
};

class RebuildListfileSeedCursor {
 public:
  explicit RebuildListfileSeedCursor(std::string buffer)
      : buffer_(std::move(buffer)) {}

  std::optional<std::string_view> Next() {
    if (offset_ < buffer_.size()) {
      const std::size_t line_start = offset_;
      while (offset_ < buffer_.size()) {
        const char ch = buffer_[offset_];
        if (ch == '\r' || ch == '\n') {
          buffer_[offset_] = '\0';
          ++offset_;
          if (ch == '\r' && offset_ < buffer_.size() && buffer_[offset_] == '\n') {
            ++offset_;
          }
          return std::string_view(buffer_.data() + line_start);
        }
        ++offset_;
      }
    }

    if (synthetic_index_ >= kRebuildSyntheticEntries.size()) {
      return std::nullopt;
    }

    return kRebuildSyntheticEntries[synthetic_index_++];
  }

 private:
  std::string buffer_;
  std::size_t offset_{0};
  std::size_t synthetic_index_{0};
};

bool HasFileDirect(HANDLE archive, const std::string& normalized_path) {
  if (archive == nullptr || normalized_path.empty()) {
    return false;
  }

  if (SFileHasFile(archive, normalized_path.c_str()) != 0) {
    return true;
  }

  const auto backslash_path = ToBackslashPath(normalized_path);
  return backslash_path != normalized_path &&
         SFileHasFile(archive, backslash_path.c_str()) != 0;
}

bool ArchiveHasListedFile(HANDLE archive, std::string_view candidate) {
  return HasFileDirect(archive, NormalizeMpqVirtualPath(std::string(candidate)));
}

bool OpenFileDirect(HANDLE archive,
                    const std::string& normalized_path,
                    const DWORD open_scope,
                    HANDLE* out_file) {
  if (archive == nullptr || normalized_path.empty() || out_file == nullptr) {
    return false;
  }

  *out_file = nullptr;
  if (SFileOpenFileEx(archive, normalized_path.c_str(), open_scope, out_file)) {
    return true;
  }

  const auto backslash_path = ToBackslashPath(normalized_path);
  return backslash_path != normalized_path &&
         SFileOpenFileEx(archive, backslash_path.c_str(), open_scope, out_file);
}

void AddCanonicalNameIfPresent(
    HANDLE archive,
    std::string_view candidate,
    std::unordered_map<std::string, std::string>& canonical_names_by_lower) {
  if (!ArchiveHasListedFile(archive, candidate)) {
    return;
  }

  const std::string canonical_name(candidate);
  const std::string normalized = NormalizeMpqVirtualPath(canonical_name);
  if (normalized.empty()) {
    return;
  }

  canonical_names_by_lower.insert_or_assign(ToLowerAsciiCopy(normalized),
                                            canonical_name);
}

bool MatchesEnumerationRoot(const std::string_view normalized_candidate,
                            const std::string_view normalized_root,
                            const bool recursive) {
  if (normalized_root.empty()) {
    return true;
  }
  if (normalized_candidate.size() <= normalized_root.size() ||
      normalized_candidate.compare(0, normalized_root.size(),
                                   normalized_root) != 0 ||
      normalized_candidate[normalized_root.size()] != '/') {
    return false;
  }
  return recursive ||
         normalized_candidate.find('/', normalized_root.size() + 1u) ==
             std::string_view::npos;
}
#endif

}

bool OpenStormArchive(const char* path,
                      const std::uint32_t priority,
                      const std::uint32_t flags,
                      void** out_archive) {
  if (out_archive == nullptr) {
    return false;
  }
  *out_archive = nullptr;

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB

  static std::once_flag initialization_once;
  HANDLE archive = nullptr;
  bool initialized_here = false;
  bool opened = false;
  std::call_once(initialization_once, [&] {
    initialized_here = true;
    opened = SFileOpenArchive(path, priority, flags, &archive);
  });
  if (!initialized_here) {
    opened = SFileOpenArchive(path, priority, flags, &archive);
  }
  if (opened) {
    *out_archive = archive;
  }
  return opened;
#else
  (void)path;
  (void)priority;
  (void)flags;
  return false;
#endif
}

ArchiveOpenProbeStatus ProbeArchiveOpenHeader(const std::filesystem::path& path,
                                             const std::uint32_t flags,
                                             ArchiveOpenProbeInfo* out_info) {
  std::error_code file_size_error;
  const auto raw_file_size = std::filesystem::file_size(path, file_size_error);
  if (file_size_error) {
    return ArchiveOpenProbeStatus::kUnavailable;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return ArchiveOpenProbeStatus::kUnavailable;
  }

  const std::uint64_t file_size = raw_file_size;
  std::array<std::uint8_t, kMpqHeaderProbeWindowSize> buffer{};
  std::uint64_t buffer_offset = 0;
  std::size_t buffer_size = 0;
  std::uint64_t scan_offset = 0;
  bool restarted_for_last_valid_header = false;
  bool has_saved_valid_header = false;
  ArchiveOpenProbeInfo saved_info{};
  ArchiveOpenProbeInfo current_probe{
      .file_size = file_size,
  };

  while (true) {
    const bool buffer_contains_offset =
        buffer_size != 0 && scan_offset >= buffer_offset
        && scan_offset - buffer_offset < buffer_size;
    if (!buffer_contains_offset) {
      if (!ReadArchiveProbeWindow(input, scan_offset, buffer, buffer_size)) {
        return ArchiveOpenProbeStatus::kUnavailable;
      }
      buffer_offset = scan_offset;
    }

    const std::uint64_t buffer_relative_offset = scan_offset - buffer_offset;
    const std::size_t available_bytes =
        buffer_relative_offset < buffer_size
            ? buffer_size - static_cast<std::size_t>(buffer_relative_offset)
            : 0;
    if (available_bytes < kMpqHeaderProbeMinimumSize
        || scan_offset + kMpqHeaderProbeMinimumSize >= file_size) {
      if ((flags & kArchiveOpenFlagSearchLastValidHeader) == 0
          || !has_saved_valid_header) {
        return ArchiveOpenProbeStatus::kNotArchive;
      }

      scan_offset = saved_info.header_offset;
      buffer_offset = 0;
      buffer_size = 0;
      restarted_for_last_valid_header = true;
      continue;
    }

    const std::uint8_t* const candidate =
        buffer.data() + static_cast<std::size_t>(buffer_relative_offset);
    const std::uint32_t signature = LoadLittleEndian32(candidate);
    if (signature == kMpqUserDataMagic) {
      current_probe.has_user_data = true;
      current_probe.user_data_payload_offset = scan_offset + 12u;
      current_probe.user_data_payload_size = LoadLittleEndian32(candidate + 4);
      current_probe.user_data_header_span = LoadLittleEndian32(candidate + 8);
      scan_offset += current_probe.user_data_header_span;
      continue;
    }

    if (signature == kMpqHeaderMagic) {
      ParsedArchiveHeader parsed_header{};
      if (ParseArchiveHeaderCandidate(candidate, file_size, parsed_header)) {
        current_probe.header_offset = scan_offset;
        current_probe.header_size = parsed_header.header_size;
        current_probe.archive_size = parsed_header.archive_size;
        current_probe.format_version = parsed_header.format_version;
        current_probe.sector_size_shift = parsed_header.sector_size_shift;
        current_probe.hash_table_offset = parsed_header.hash_table_offset;
        current_probe.block_table_offset = parsed_header.block_table_offset;
        current_probe.extended_block_table_offset =
            parsed_header.extended_block_table_offset;
        current_probe.hash_table_entry_count =
            parsed_header.hash_table_entry_count;
        current_probe.block_table_entry_count =
            parsed_header.block_table_entry_count;
        current_probe.table_region_end = scan_offset + parsed_header.table_region_end;
        current_probe.sector_size =
            512u << (parsed_header.sector_size_shift & 31u);

        saved_info = current_probe;
        has_saved_valid_header = true;
        if (restarted_for_last_valid_header
            || (flags & kArchiveOpenFlagSearchLastValidHeader) == 0) {
          if (out_info != nullptr) {
            *out_info = saved_info;
          }
          return ArchiveOpenProbeStatus::kSuccess;
        }
      }
    }

    if ((flags & kArchiveOpenFlagStrictHeaderOffset) != 0) {
      return ArchiveOpenProbeStatus::kNotArchive;
    }

    scan_offset += kMpqHeaderProbeStride;
  }
}

MpqArchive::~MpqArchive() {
  Close();
}

bool MpqArchive::Open(const std::filesystem::path& path) {
  std::lock_guard lock(mutex_);
  CloseUnlocked();

  if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
    last_error_ = 0;
    return false;
  }

  if (ProbeArchiveOpenHeader(path, 0) == ArchiveOpenProbeStatus::kNotArchive) {
    last_error_ = kStormErrorNotArchive;
    return false;
  }

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  void* archive = nullptr;
  if (!OpenStormArchive(path.string().c_str(), 0, STREAM_FLAG_READ_ONLY,
                        &archive)) {
    last_error_ = static_cast<std::uint32_t>(GetLastError());
    return false;
  }

  archive_path_ = path;
  archive_handle_ = archive;
  is_open_ = true;
  last_error_ = 0;
  index_ready_ = false;
  canonical_names_by_lower_.clear();
  return true;
#else
  (void)path;
  is_open_ = false;
  last_error_ = 0;
  return false;
#endif
}

bool MpqArchive::ApplyPatch(const std::filesystem::path& patch_path) {
  std::lock_guard lock(mutex_);
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    last_error_ = 0;
    return false;
  }
  if (!std::filesystem::exists(patch_path) || !std::filesystem::is_regular_file(patch_path)) {
    last_error_ = 0;
    return false;
  }

  const char *prefix = "";
  const bool ok = SFileOpenPatchArchive(static_cast<HANDLE>(archive_handle_),
                                        patch_path.string().c_str(),
                                        prefix,
                                        0);
  if (ok) {

    last_error_ = 0;
    index_ready_ = false;
    canonical_names_by_lower_.clear();
    has_patches_ = true;
  } else {
    last_error_ = static_cast<std::uint32_t>(GetLastError());
  }
  return ok;
#else
  (void)patch_path;
  last_error_ = 0;
  return false;
#endif
}

void MpqArchive::Close() {
  std::lock_guard lock(mutex_);
  CloseUnlocked();
}

void MpqArchive::CloseUnlocked() {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (archive_handle_ != nullptr) {
    SFileCloseArchive(static_cast<HANDLE>(archive_handle_));
  }
#endif
  archive_handle_ = nullptr;
  archive_path_.clear();
  is_open_ = false;
  has_patches_ = false;
  last_error_ = 0;
  index_ready_ = false;
  canonical_names_by_lower_.clear();
}

bool MpqArchive::IsOpen() const {
  std::lock_guard lock(mutex_);
  return is_open_;
}

bool MpqArchive::has_patches() const {
  std::lock_guard lock(mutex_);
  return has_patches_;
}

std::filesystem::path MpqArchive::archive_path() const {
  std::lock_guard lock(mutex_);
  return archive_path_;
}

std::uint32_t MpqArchive::last_error() const {
  std::lock_guard lock(mutex_);
  return last_error_;
}

bool MpqArchive::Exists(const std::string& virtual_path) const {
  std::lock_guard lock(mutex_);
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    return false;
  }

  const auto normalized = NormalizeVirtualPath(virtual_path);
  if (normalized.empty()) {
    return false;
  }

  if (HasDeleteMarkerUnlocked(normalized)) {
    return false;
  }

  if (has_patches_) {

    HANDLE file = nullptr;
    const bool opened = OpenFileDirect(static_cast<HANDLE>(archive_handle_),
                                       normalized,
                                       SFILE_OPEN_PATCHED_FILE,
                                       &file);
    if (file != nullptr) {
      SFileCloseFile(file);
    }
    return opened;
  }

  return HasFileDirect(static_cast<HANDLE>(archive_handle_), normalized);
#else
  (void)virtual_path;
  return false;
#endif
}

bool MpqArchive::HasDeleteMarkerUnlocked(
    const std::string& normalized_path) const {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr || normalized_path.empty()) {
    return false;
  }

  const auto probe = [&](const DWORD scope) {
    HANDLE file = nullptr;
    if (!OpenFileDirect(static_cast<HANDLE>(archive_handle_), normalized_path,
                        scope, &file) ||
        file == nullptr) {
      return false;
    }
    DWORD flags = 0;

    const bool has_flags = SFileGetFileInfo(file, SFILE_INFO_FLAGS, &flags,
                                            sizeof(flags), nullptr) != 0;
    SFileCloseFile(file);
    return has_flags && (flags & MPQ_FILE_DELETE_MARKER) != 0u;
  };
  if (has_patches_ && probe(SFILE_OPEN_PATCHED_FILE)) {
    return true;
  }
  return probe(SFILE_OPEN_FROM_MPQ);
#else
  (void)normalized_path;
  return false;
#endif
}

bool MpqArchive::HasDeleteMarker(const std::string& virtual_path) const {
  std::lock_guard lock(mutex_);
  return HasDeleteMarkerUnlocked(NormalizeVirtualPath(virtual_path));
}

std::optional<std::vector<std::uint8_t>> MpqArchive::ReadFile(
    const std::string& virtual_path) const {
  std::lock_guard lock(mutex_);
  return ReadFileUnlocked(virtual_path, std::nullopt);
}

std::optional<std::vector<std::uint8_t>> MpqArchive::ReadFilePrefix(
    const std::string& virtual_path, const std::size_t max_bytes) const {
  std::lock_guard lock(mutex_);
  return ReadFileUnlocked(virtual_path, max_bytes);
}

std::optional<std::vector<std::uint8_t>> MpqArchive::ReadFileUnlocked(
    const std::string& virtual_path,
    const std::optional<std::size_t> max_bytes) const {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    last_error_ = 0;
    return std::nullopt;
  }

  const auto normalized = NormalizeVirtualPath(virtual_path);
  if (normalized.empty()) {
    last_error_ = 0;
    return std::nullopt;
  }

  if (HasDeleteMarkerUnlocked(normalized)) {
    last_error_ = 0;
    return std::nullopt;
  }

  HANDLE file = nullptr;

  const DWORD open_scope = has_patches_ ? SFILE_OPEN_PATCHED_FILE : SFILE_OPEN_FROM_MPQ;
  if (!OpenFileDirect(static_cast<HANDLE>(archive_handle_), normalized, open_scope, &file) ||
      file == nullptr) {
    last_error_ = static_cast<std::uint32_t>(GetLastError());
    return std::nullopt;
  }
  last_error_ = 0;

  DWORD size_high = 0;
  const DWORD size_low = SFileGetFileSize(file, &size_high);
  if (size_low == SFILE_INVALID_SIZE || size_high != 0) {
    last_error_ = static_cast<std::uint32_t>(GetLastError());
    SFileCloseFile(file);
    return std::nullopt;
  }

  const auto requested_size = static_cast<DWORD>(
      std::min<std::uint64_t>(
          size_low, max_bytes.has_value() ? *max_bytes : size_low));
  std::vector<std::uint8_t> bytes(requested_size);
  DWORD read = 0;
  const int ok = requested_size == 0u ||
                 SFileReadFile(file, bytes.data(), requested_size, &read,
                               nullptr);
  SFileCloseFile(file);
  if (!ok) {
    last_error_ = static_cast<std::uint32_t>(GetLastError());
    return std::nullopt;
  }
  bytes.resize(read);
  return bytes;
#else
  (void)virtual_path;
  (void)max_bytes;
  last_error_ = 0;
  return std::nullopt;
#endif
}

std::vector<std::string> MpqArchive::EnumerateFiles(const std::string& virtual_root,
                                                    bool recursive) const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    return out;
  }

  auto normalized_root = ToLowerAscii(NormalizeVirtualPath(virtual_root));
  while (!normalized_root.empty() && normalized_root.back() == '/') {
    normalized_root.pop_back();
  }
  std::unordered_map<std::string, std::string> root_index;
  const std::unordered_map<std::string, std::string>* index = nullptr;
  if (index_ready_ || normalized_root.empty()) {
    BuildFileIndexUnlocked();
    index = &canonical_names_by_lower_;
  } else {

    CollectFileIndexForRootUnlocked(normalized_root, recursive, root_index);
    index = &root_index;
  }

  std::string root_prefix = "/";
  if (!normalized_root.empty()) {
    root_prefix = normalized_root;
    if (!root_prefix.empty() && root_prefix.front() != '/') {
      root_prefix.insert(root_prefix.begin(), '/');
    }
    if (!root_prefix.empty() && root_prefix.back() != '/') {
      root_prefix.push_back('/');
    }
  }

  std::vector<std::pair<std::string, std::string>> matches;
  matches.reserve(index->size());
  for (const auto& [key, canonical] : *index) {
    const std::string lookup_name = "/" + key;
    if (lookup_name.rfind(root_prefix, 0) != 0) {
      continue;
    }
    if (!recursive) {
      const auto rest = lookup_name.substr(root_prefix.size());
      if (rest.find('/') != std::string::npos) {
        continue;
      }
    }

    matches.emplace_back(lookup_name,
                         "/" + NormalizeVirtualPath(canonical));
  }
  std::sort(matches.begin(), matches.end(), [](const auto& lhs,
                                                const auto& rhs) {
    return lhs.first < rhs.first;
  });
  out.reserve(matches.size());
  for (auto& [lookup_name, display_name] : matches) {
    (void)lookup_name;
    out.push_back(std::move(display_name));
  }
#else
  (void)virtual_root;
  (void)recursive;
#endif
  return out;
}

std::vector<std::string> MpqArchive::EnumerateDeleteMarkedFiles(
    const std::string& virtual_root, const bool recursive) const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> out;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    return out;
  }

  auto normalized_root = ToLowerAscii(NormalizeVirtualPath(virtual_root));
  while (!normalized_root.empty() && normalized_root.back() == '/') {
    normalized_root.pop_back();
  }

  HANDLE listfile = nullptr;
  if (!SFileOpenFileEx(static_cast<HANDLE>(archive_handle_), "(listfile)",
                       SFILE_OPEN_FROM_MPQ, &listfile)) {
    return out;
  }
  DWORD size_high = 0;
  const DWORD size_low = SFileGetFileSize(listfile, &size_high);
  if (size_low != SFILE_INVALID_SIZE && size_high == 0) {
    std::string content;
    content.resize(size_low);
    DWORD read = 0;
    if (SFileReadFile(listfile, content.data(), size_low, &read, nullptr)) {
      content.resize(read);
      RebuildListfileSeedCursor cursor(std::move(content));
      while (const auto entry = cursor.Next()) {
        const std::string normalized =
            NormalizeMpqVirtualPath(std::string(*entry));
        const std::string normalized_candidate = ToLowerAsciiCopy(normalized);
        if (!MatchesEnumerationRoot(normalized_candidate, normalized_root,
                                    recursive)) {
          continue;
        }
        if (HasDeleteMarkerUnlocked(normalized)) {
          out.push_back("/" + normalized);
        }
      }
    }
  }
  SFileCloseFile(listfile);
#else
  (void)virtual_root;
  (void)recursive;
#endif
  return out;
}

std::string MpqArchive::NormalizeVirtualPath(const std::string& virtual_path) {
  return NormalizeMpqVirtualPath(virtual_path);
}

std::string MpqArchive::ToLowerAscii(std::string value) {
  return ToLowerAsciiCopy(std::move(value));
}

void MpqArchive::BuildFileIndexUnlocked() const {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr || index_ready_) {
    return;
  }

  canonical_names_by_lower_.clear();
  CollectFileIndexForRootUnlocked({}, true, canonical_names_by_lower_);
  index_ready_ = true;
#endif
}

void MpqArchive::CollectFileIndexForRootUnlocked(
    const std::string& normalized_root,
    const bool recursive,
    std::unordered_map<std::string, std::string>& out) const {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!is_open_ || archive_handle_ == nullptr) {
    return;
  }

  HANDLE listfile = nullptr;
  if (SFileOpenFileEx(static_cast<HANDLE>(archive_handle_),
                      "(listfile)",
                      SFILE_OPEN_FROM_MPQ,
                      &listfile)) {
    DWORD size_high = 0;
    const DWORD size_low = SFileGetFileSize(listfile, &size_high);
    if (size_low != SFILE_INVALID_SIZE && size_high == 0) {
      std::string content;
      content.resize(size_low);
      DWORD read = 0;
      if (SFileReadFile(listfile, content.data(), size_low, &read, nullptr)) {
        content.resize(read);
        RebuildListfileSeedCursor cursor(std::move(content));
        while (const auto entry = cursor.Next()) {
          const std::string normalized_candidate = ToLowerAsciiCopy(
              NormalizeMpqVirtualPath(std::string(*entry)));
          if (!MatchesEnumerationRoot(normalized_candidate,
                                      normalized_root,
                                      recursive)) {
            continue;
          }

          if (HasDeleteMarkerUnlocked(
                  NormalizeMpqVirtualPath(std::string(*entry)))) {
            continue;
          }
          AddCanonicalNameIfPresent(static_cast<HANDLE>(archive_handle_),
                                    *entry,
                                    out);
        }
      }
    }
    SFileCloseFile(listfile);
  }

  for (const auto tracked_name : kTrackedInternalEntries) {
    const std::string normalized_candidate =
        ToLowerAsciiCopy(NormalizeMpqVirtualPath(std::string(tracked_name)));
    if (!MatchesEnumerationRoot(normalized_candidate,
                                normalized_root,
                                recursive)) {
      continue;
    }
    AddCanonicalNameIfPresent(static_cast<HANDLE>(archive_handle_),
                              tracked_name,
                              out);
  }
#else
  (void)normalized_root;
  (void)recursive;
  (void)out;
#endif
}

}

#include "openwow/vfs/retail/archive_registry.h"

#include "openwow/core/mpq_internals.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/archive_system.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/adapters/mpq/mpq_archive.h"
#include "openwow/vfs/retail/sfile_types.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

namespace openwow::vfs {
namespace {

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#define SFileMpqFileName        SFILE_INFO_ARCHIVE_NAME
#define SFileInfoFlags          SFILE_INFO_FLAGS
#define SFileInfoCompressedSize SFILE_INFO_COMPRESSED_SIZE
#define SFileInfoByteOffset     SFILE_INFO_BLOCKINDEX
constexpr auto kStormInfoArchiveName = SFileMpqFileName;
constexpr auto kStormInfoFileFlags = SFileInfoFlags;
constexpr auto kStormInfoCompressedSize = SFileInfoCompressedSize;
constexpr auto kStormInfoPosition = SFileInfoByteOffset;
#endif

enum class ArchiveKind : std::uint8_t {
  kMpq,
  kDirectory,
};

struct ArchiveDescriptor {
  ArchiveKind kind = ArchiveKind::kMpq;
  void *raw_handle = nullptr;
  std::string source_path;
  std::filesystem::path patch_resolution_path;
  std::string base_archive_path;
  std::vector<std::filesystem::path> applied_patch_paths;
  std::uint32_t open_flags = 0;
  std::int32_t priority = 0;
  std::int32_t open_sequence = 0;
};

struct ArchiveCloseLifetime {
  bool CloseRaw(void *archive) {
    if (!archive) {
      return false;
    }
    if (raw_close_hook) {
      return raw_close_hook(archive);
    }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    return SFileCloseArchive(static_cast<HANDLE>(archive)) != 0;
#else
    return false;
#endif
  }

  ArchiveRegistry::RawCloseHook raw_close_hook;
};

struct ArchiveState {
  ArchiveState(ArchiveDescriptor descriptor_in,
               std::shared_ptr<ArchiveCloseLifetime> close_lifetime_in)
      : descriptor(std::move(descriptor_in)), close_lifetime(std::move(close_lifetime_in)) {}
  ~ArchiveState() { (void)Close(); }

  bool Close() {
    std::lock_guard lock(close_mutex);
    if (close_attempted) {
      return close_result;
    }
    close_attempted = true;
    if (descriptor.kind == ArchiveKind::kDirectory) {
      descriptor.raw_handle = nullptr;
      close_result = true;
      return true;
    }
    close_result = close_lifetime->CloseRaw(std::exchange(descriptor.raw_handle, nullptr));
    return close_result;
  }

  ArchiveDescriptor descriptor;
  std::shared_ptr<ArchiveCloseLifetime> close_lifetime;
  std::recursive_mutex operation_mutex;
  std::mutex close_mutex;
  bool close_attempted = false;
  bool close_result = true;
};

using ArchiveHandle = std::shared_ptr<ArchiveState>;

struct OrderKey {
  std::int32_t priority = 0;
  std::int32_t open_sequence = 0;
};

bool operator<(const OrderKey &lhs, const OrderKey &rhs) {

  if (lhs.priority != rhs.priority) {
    return lhs.priority > rhs.priority;
  }
  return lhs.open_sequence > rhs.open_sequence;
}

OrderKey MakeOrderKey(const ArchiveDescriptor &descriptor) {
  return {.priority = descriptor.priority, .open_sequence = descriptor.open_sequence};
}

class ArchiveAccess {
public:
  ArchiveAccess() = default;
  explicit ArchiveAccess(ArchiveHandle archive) : archive_(std::move(archive)) {}
  ArchiveAccess(ArchiveHandle archive, std::unique_lock<std::recursive_mutex> operation_lock)
      : archive_(std::move(archive)), operation_lock_(std::move(operation_lock)) {}

  explicit operator bool() const noexcept { return static_cast<bool>(archive_); }
  const ArchiveHandle &handle() const noexcept { return archive_; }
  const ArchiveDescriptor &descriptor() const noexcept { return archive_->descriptor; }
  void *raw_handle() const noexcept {
    return archive_ ? archive_->descriptor.raw_handle : nullptr;
  }

private:
  ArchiveHandle archive_;
  std::unique_lock<std::recursive_mutex> operation_lock_;
};

struct SnapshotEntry {
  std::uint32_t token = 0;
  ArchiveHandle handle;
};

std::filesystem::path ResolveNativePath(const std::string_view path) {
  const std::string null_terminated(path);
  return ToNativePath(null_terminated.c_str());
}

std::array<std::string, 2> BuildArchiveOpenCandidates(const char *filename) {
  std::array<std::string, 2> candidates = {
      std::string(filename ? filename : ""),
      std::string(filename ? filename : ""),
  };
  std::replace(candidates[1].begin(), candidates[1].end(), '/', '\\');
  return candidates;
}

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
bool TryOpenArchiveFileHandle(void *raw_archive, const char *filename, void **out_file_handle) {
  if (!raw_archive || !filename || !out_file_handle) {
    return false;
  }

  *out_file_handle = nullptr;
  for (const auto &candidate : BuildArchiveOpenCandidates(filename)) {
    if (candidate.empty()) {
      continue;
    }
    HANDLE file = nullptr;
    if (SFileOpenFileEx(static_cast<HANDLE>(raw_archive), candidate.c_str(), SFILE_OPEN_FROM_MPQ,
                        &file)) {
      *out_file_handle = file;
      return true;
    }
  }

  openwow::core::StormSetLastError(static_cast<int>(openwow::platform::GetPlatformLastError()));
  return false;
}
#endif

bool QueryRawArchivePath(void *raw_archive, std::string *out_path) {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!raw_archive || !out_path) {
    return false;
  }
  out_path->clear();
  char archive_name[1024] = {};
  if (!SFileGetFileInfo(static_cast<HANDLE>(raw_archive), kStormInfoArchiveName, archive_name,
                        sizeof(archive_name), nullptr)) {
    return false;
  }
  *out_path = ToUtf8FilesystemPathString(std::filesystem::path(archive_name));
  return true;
#else
  (void)raw_archive;
  (void)out_path;
  return false;
#endif
}

std::optional<std::filesystem::path> ResolvePatchedArchivePath(
    const ArchiveDescriptor &base_archive, const char *patch_path) {
  if (!patch_path || *patch_path == '\0') {
    return std::nullopt;
  }
  const std::filesystem::path native_patch = ResolveNativePath(patch_path);
  if (native_patch.is_absolute()) {
    return native_patch;
  }
  const std::filesystem::path resolution_base = base_archive.patch_resolution_path.empty()
                                                    ? ResolveNativePath(base_archive.source_path)
                                                    : base_archive.patch_resolution_path;
  return resolution_base.empty() ? native_patch : resolution_base.parent_path() / native_patch;
}

std::string DerivePatchArchivePrefix(const std::filesystem::path &patch_path) {
  std::string prefix = patch_path.stem().string();
  const auto dash = prefix.rfind('-');
  if (dash == std::string::npos) {
    return prefix;
  }
  bool numeric_suffix = dash + 1 < prefix.size();
  for (std::size_t index = dash + 1; index < prefix.size(); ++index) {
    if (prefix[index] < '0' || prefix[index] > '9') {
      numeric_suffix = false;
      break;
    }
  }
  if (numeric_suffix) {
    prefix.resize(dash);
  }
  return prefix;
}

struct AuthenticationKeyView {
  const std::uint8_t *modulus = nullptr;
  int modulus_size = 0;
  const std::uint8_t *exponent = nullptr;
  int exponent_size = 0;
};

enum class AuthenticationOutcome {
  kVerified,
  kNotAuthenticated,
  kTrailerUnavailable,
  kTransportFailure,
};

struct AuthenticationRange {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
};

constexpr std::array<std::uint8_t, 4> kDefaultExponent = {0x01, 0x00, 0x01, 0x00};
constexpr std::array<std::uint8_t, 0x100> kDefaultModulus = {
    0x77, 0x64, 0xf8, 0x57, 0x1d, 0xfb, 0xb0, 0x09, 0xc4, 0xe6, 0x28, 0x91, 0x34, 0xe3, 0x55,
    0x61, 0x15, 0x8a, 0xe9, 0x07, 0xfc, 0xaa, 0x60, 0xb3, 0x82, 0xb7, 0xe2, 0xa4, 0x40, 0x15,
    0x01, 0x3f, 0xc2, 0x36, 0xa8, 0x9d, 0x95, 0xd0, 0x54, 0x69, 0xaa, 0xf5, 0xed, 0x5c, 0x7f,
    0x21, 0xc5, 0x55, 0x95, 0x56, 0x5b, 0x2f, 0xc6, 0xdd, 0x2c, 0xbd, 0x74, 0xa3, 0x5a, 0x0d,
    0x70, 0x98, 0x9a, 0x01, 0x36, 0x51, 0x78, 0x71, 0x9b, 0x8e, 0xcb, 0xb8, 0x84, 0x67, 0x30,
    0xf4, 0x43, 0xb3, 0xa3, 0x50, 0xa3, 0xba, 0xa4, 0xf7, 0xb1, 0x94, 0xe5, 0x5b, 0x95, 0x8b,
    0x1a, 0xe4, 0x04, 0x1d, 0xfb, 0xcf, 0x0e, 0xe6, 0x97, 0x4c, 0xdc, 0xe4, 0x28, 0x7f, 0xb8,
    0x58, 0x4a, 0x45, 0x1b, 0xc8, 0x8c, 0xd0, 0xfd, 0x2e, 0x77, 0xc4, 0x30, 0xd8, 0x3d, 0xd2,
    0xd5, 0xfa, 0xba, 0x9d, 0x1e, 0x02, 0xf6, 0x7b, 0xbe, 0x08, 0x95, 0xcb, 0xb0, 0x53, 0x3e,
    0x1c, 0x41, 0x45, 0xfc, 0x27, 0x6f, 0x63, 0x6a, 0x73, 0x91, 0xa9, 0x42, 0x00, 0x12, 0x93,
    0xf8, 0x5b, 0x83, 0xed, 0x52, 0x77, 0x4e, 0x38, 0x08, 0x16, 0x23, 0x10, 0x85, 0x4c, 0x0b,
    0xa9, 0x8c, 0x9c, 0x40, 0x4c, 0xaf, 0x6e, 0xa7, 0x89, 0x02, 0xc5, 0x06, 0x96, 0x99, 0x41,
    0xd4, 0x31, 0x03, 0x4a, 0xa9, 0x2b, 0x17, 0x52, 0xdd, 0x5c, 0x4e, 0x5f, 0x16, 0xc3, 0x81,
    0x0f, 0x2e, 0xe2, 0x17, 0x45, 0x2b, 0x7b, 0x65, 0x7a, 0xa3, 0x18, 0x87, 0xc2, 0xb2, 0xf5,
    0xcd, 0x9c, 0xba, 0xcb, 0xde, 0x07, 0x6f, 0x7c, 0x8b, 0x03, 0x68, 0xe6, 0x3c, 0x5a, 0x2c,
    0xae, 0xdc, 0xc3, 0xc8, 0x38, 0x35, 0x82, 0xda, 0x4d, 0x04, 0xce, 0x9c, 0x68, 0x47, 0x7d,
    0xb4, 0x1d, 0x98, 0x42, 0x8c, 0xf8, 0x27, 0x7e, 0xc8, 0x87, 0xf6, 0x24, 0xce, 0x7e, 0x06,
    0xb1,
};

std::optional<AuthenticationRange> ResolveAuthenticationRange(void *raw_archive) {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  auto *archive = static_cast<TMPQArchive *>(raw_archive);
  if (!archive || !archive->pHeader || archive->pHeader->ArchiveSize64 == 0 ||
      archive->MpqPos > std::numeric_limits<std::uint64_t>::max() -
                            archive->pHeader->ArchiveSize64) {
    return std::nullopt;
  }
  return AuthenticationRange{.begin = archive->MpqPos,
                             .end = archive->MpqPos + archive->pHeader->ArchiveSize64};
#else
  (void)raw_archive;
  return std::nullopt;
#endif
}

AuthenticationKeyView ResolveAuthenticationKey(const void *modulus, int modulus_size,
                                                const void *exponent, int exponent_size) {
  const bool has_modulus = modulus && modulus_size > 0;
  const bool has_exponent = exponent && exponent_size > 0;
  if (!has_modulus && !has_exponent) {
    return {.modulus = kDefaultModulus.data(),
            .modulus_size = static_cast<int>(kDefaultModulus.size()),
            .exponent = kDefaultExponent.data(),
            .exponent_size = static_cast<int>(kDefaultExponent.size())};
  }
  return {.modulus = static_cast<const std::uint8_t *>(modulus),
          .modulus_size = modulus_size,
          .exponent = static_cast<const std::uint8_t *>(exponent),
          .exponent_size = exponent_size};
}

AuthenticationOutcome VerifyAuthenticationSignature(const std::string &archive_path,
                                                      void *raw_archive,
                                                      const AuthenticationKeyView &key,
                                                      const char *suffix) {
  if (archive_path.empty()) {
    return AuthenticationOutcome::kNotAuthenticated;
  }
  const auto range = ResolveAuthenticationRange(raw_archive);
  if (!range || range->begin > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
      range->end > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    return AuthenticationOutcome::kTransportFailure;
  }

  const bool can_verify = key.modulus && key.modulus_size > 0 && key.exponent && key.exponent_size > 0;
  std::ifstream input(ResolveNativePath(archive_path), std::ios::binary);
  if (!input.is_open()) {
    openwow::core::StormSetLastError(2);
    return AuthenticationOutcome::kTransportFailure;
  }

  void *context = nullptr;
  if (can_verify) {
    openwow::core::SSignature_Create(&context, key.modulus_size, key.exponent_size);
    if (!context) {
      openwow::core::StormSetLastError(8);
      return AuthenticationOutcome::kTransportFailure;
    }
  }
  if (!context) {
    return AuthenticationOutcome::kNotAuthenticated;
  }
  const auto destroy_context = [&context]() {
    if (context) {
      (void)openwow::core::SSignature_Verify(context, nullptr, nullptr);
      context = nullptr;
    }
  };

  std::array<std::uint8_t, 0x10000> buffer{};
  input.seekg(static_cast<std::streamoff>(range->begin), std::ios::beg);
  if (!input) {
    openwow::core::StormSetLastError(30);
    destroy_context();
    return AuthenticationOutcome::kTransportFailure;
  }
  std::uint64_t remaining = range->end - range->begin;
  while (remaining != 0) {
    const auto requested =
        static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
    input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(requested));
    if (input.gcount() != static_cast<std::streamsize>(requested)) {
      openwow::core::StormSetLastError(30);
      destroy_context();
      return AuthenticationOutcome::kTransportFailure;
    }
    openwow::core::SSignature_Update(context, buffer.data(), requested);
    remaining -= requested;
  }

  const std::string_view suffix_view = suffix ? std::string_view(suffix) : std::string_view{};
  openwow::core::SSignature_Update(context, suffix_view.data(), suffix_view.size());
  const int trailer_size = openwow::core::SSignature_GetTrailingSize(context);
  if (trailer_size <= 0 || static_cast<std::size_t>(trailer_size) > buffer.size()) {
    destroy_context();
    return AuthenticationOutcome::kTransportFailure;
  }
  input.clear();
  input.seekg(static_cast<std::streamoff>(range->end), std::ios::beg);
  if (!input) {
    openwow::core::StormSetLastError(30);
    destroy_context();
    return AuthenticationOutcome::kTrailerUnavailable;
  }
  input.read(reinterpret_cast<char *>(buffer.data()), trailer_size);
  if (input.gcount() != trailer_size) {
    openwow::core::StormSetLastError(30);
    destroy_context();
    return AuthenticationOutcome::kTrailerUnavailable;
  }
  openwow::core::SSignature_Update(context, buffer.data(), static_cast<std::size_t>(trailer_size));
  const bool verified = openwow::core::SSignature_Verify(context, key.modulus, key.exponent);
  context = nullptr;
  return verified ? AuthenticationOutcome::kVerified : AuthenticationOutcome::kNotAuthenticated;
}

}

class ArchiveRegistry::Impl {
public:
  ~Impl() { handles.clear(); }

  ArchiveHandle Resolve(std::uint32_t token) const {
    if (!token) {
      return {};
    }
    std::lock_guard lock(mutex);
    const auto it = handles.find(token);
    return it == handles.end() ? ArchiveHandle{} : it->second;
  }

  ArchiveAccess Acquire(const ArchiveHandle &archive, bool lock_operation) const {
    if (!archive) {
      return {};
    }
    if (lock_operation && archive->descriptor.kind == ArchiveKind::kMpq) {
      return ArchiveAccess(archive, std::unique_lock<std::recursive_mutex>(archive->operation_mutex));
    }
    return ArchiveAccess(archive);
  }

  ArchiveHandle RetainRaw(void *raw_archive) const {
    std::lock_guard lock(mutex);
    for (const auto &[_, archive] : handles) {
      if (archive && archive->descriptor.raw_handle == raw_archive) {
        return archive;
      }
    }
    return {};
  }

  std::vector<SnapshotEntry> Snapshot() const {
    std::lock_guard lock(mutex);
    std::vector<SnapshotEntry> snapshot;
    snapshot.reserve(order.size());
    for (const auto &[_, token] : order) {
      const auto it = handles.find(token);
      if (it != handles.end() && it->second) {
        snapshot.push_back({.token = token, .handle = it->second});
      }
    }
    return snapshot;
  }

  bool CloseRaw(void *archive) {
    return close_lifetime->CloseRaw(archive);
  }

  ArchiveHandle Create(ArchiveDescriptor descriptor) {
    return std::make_shared<ArchiveState>(std::move(descriptor), close_lifetime);
  }

  std::uint32_t Register(ArchiveDescriptor descriptor) {
    std::lock_guard lock(mutex);
    std::uint32_t token = 0;
    do {
      token = next_token++;
      if (next_token == 0) {
        next_token = 1;
      }
    } while (token == 0 || handles.contains(token));

    OrderKey key{};
    do {
      key = {.priority = descriptor.priority,
             .open_sequence = static_cast<std::int32_t>(next_open_sequence_raw++)};
    } while (order.contains(key));
    descriptor.open_sequence = key.open_sequence;
    handles.emplace(token, Create(std::move(descriptor)));
    order.emplace(key, token);
    return token;
  }

  ArchiveHandle Unregister(std::uint32_t token) {
    if (!token) {
      return {};
    }
    std::lock_guard lock(mutex);
    const auto it = handles.find(token);
    if (it == handles.end()) {
      return {};
    }
    auto archive = it->second;
    order.erase(MakeOrderKey(archive->descriptor));
    handles.erase(it);
    return archive;
  }

  bool ApplyPatch(void *raw_archive, const std::filesystem::path &patch_path) {
    const std::string prefix = DerivePatchArchivePrefix(patch_path);
    const std::string native_patch_path = patch_path.string();
    if (raw_patch_hook) {
      return raw_patch_hook(raw_archive, native_patch_path.c_str(), prefix.c_str());
    }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    const bool ok = SFileOpenPatchArchive(static_cast<HANDLE>(raw_archive),
                                          native_patch_path.c_str(), prefix.c_str(), 0) != 0;
    if (!ok) {
      openwow::core::StormSetLastError(
          static_cast<int>(openwow::platform::GetPlatformLastError()));
    }
    return ok;
#else
    (void)raw_archive;
    (void)patch_path;
    return false;
#endif
  }

  bool PopulateDirectoryLookup(const ArchiveDescriptor &archive, const char *filename,
                               std::uint32_t token,
                               const DirectoryMemberResolver &resolve_directory_member,
                               SFileArchiveLookupInfo *out_info) const {
    if (!out_info || !resolve_directory_member) {
      return false;
    }
    const auto native_path = resolve_directory_member(archive.source_path, filename);
    if (!native_path) {
      return false;
    }
    *out_info = {};
    out_info->archive_token = token;
    out_info->resolved_path = *native_path;
    out_info->archive_path = archive.source_path;
    std::error_code ec;
    const auto size = std::filesystem::file_size(*native_path, ec);
    if (!ec) {
      out_info->compressed_size = static_cast<std::uint32_t>(
          std::min<std::uintmax_t>(size, std::numeric_limits<std::uint32_t>::max()));
      out_info->file_size = out_info->compressed_size;
    }
    return true;
  }

  bool PopulateMpqLookup(void *raw_archive, const char *filename,
                         const std::string *known_archive_path, std::uint32_t token,
                         SFileArchiveLookupInfo *out_info,
                         SFileArchiveLookupResult *out_result) const {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    if (!raw_archive || !filename || !*filename || !out_info || !out_result) {
      return false;
    }
    HANDLE file = nullptr;
    if (!TryOpenArchiveFileHandle(raw_archive, filename, reinterpret_cast<void **>(&file))) {
      return false;
    }
    SFileArchiveLookupInfo info;
    info.archive_token = token;
    if (known_archive_path && !known_archive_path->empty()) {
      info.archive_path = *known_archive_path;
    } else {
      (void)QueryRawArchivePath(raw_archive, &info.archive_path);
    }
    DWORD flags = 0;
    if (SFileGetFileInfo(file, kStormInfoFileFlags, &flags, sizeof(flags), nullptr)) {
      info.file_flags = flags;
    }
    DWORD size_high = 0;
    const DWORD size_low = SFileGetFileSize(file, &size_high);
    if (size_low != SFILE_INVALID_SIZE && size_high == 0) {
      info.file_size = size_low;
    }
    DWORD compressed_size = 0;
    if (SFileGetFileInfo(file, kStormInfoCompressedSize, &compressed_size,
                         sizeof(compressed_size), nullptr)) {
      info.compressed_size = compressed_size;
    }
    ULONGLONG position = 0;
    if (SFileGetFileInfo(file, kStormInfoPosition, &position, sizeof(position), nullptr)) {
      info.file_position = static_cast<std::uint64_t>(position);
    }
    SFileCloseFile(file);
    *out_info = std::move(info);
    *out_result = (flags & 0x02000000u) != 0u ? SFileArchiveLookupResult::kPatchMarker
                                              : SFileArchiveLookupResult::kArchive;
    return true;
#else
    (void)raw_archive;
    (void)filename;
    (void)known_archive_path;
    (void)token;
    (void)out_info;
    (void)out_result;
    return false;
#endif
  }

  SFileArchiveLookupResult LookupAccess(const ArchiveAccess &access, const char *filename,
                                        std::uint32_t token,
                                        const DirectoryMemberResolver &resolver,
                                        SFileArchiveLookupInfo *out_info) const {
    if (!access || !filename || !*filename) {
      return SFileArchiveLookupResult::kMiss;
    }
    if (access.descriptor().kind == ArchiveKind::kDirectory) {
      return PopulateDirectoryLookup(access.descriptor(), filename, token, resolver, out_info)
                 ? SFileArchiveLookupResult::kDirectoryArchive
                 : SFileArchiveLookupResult::kMiss;
    }
    if (!access.raw_handle()) {
      return SFileArchiveLookupResult::kMiss;
    }
    SFileArchiveLookupResult result = SFileArchiveLookupResult::kMiss;
    return PopulateMpqLookup(access.raw_handle(), filename, &access.descriptor().source_path,
                             token, out_info, &result)
               ? result
               : SFileArchiveLookupResult::kMiss;
  }

  mutable std::mutex mutex;
  std::unordered_map<std::uint32_t, ArchiveHandle> handles;
  std::map<OrderKey, std::uint32_t> order;
  std::uint32_t next_token = 1;
  std::uint32_t next_open_sequence_raw = 0;
  std::shared_ptr<ArchiveCloseLifetime> close_lifetime =
      std::make_shared<ArchiveCloseLifetime>();
  RawOpenHook raw_open_hook;
  RawPatchHook raw_patch_hook;
};

ArchiveRegistry::ArchiveRegistry() : impl_(std::make_unique<Impl>()) {}
ArchiveRegistry::~ArchiveRegistry() = default;

ArchiveRegistry &RetailArchiveRegistry() {
  static ArchiveRegistry registry;
  return registry;
}

bool ArchiveRegistry::OpenRawArchive(const char *path, std::int32_t priority, std::uint32_t flags,
                                     void **out_handle) {
  if (!out_handle) {
    return false;
  }
  *out_handle = nullptr;
  if (impl_->raw_open_hook) {
    return impl_->raw_open_hook(path, priority, flags, out_handle);
  }
  std::filesystem::path native_path;
  if (path) {
    native_path = ResolveNativePath(path);
    const auto probe = mpq::ProbeArchiveOpenHeader(native_path, flags);
    if (probe == mpq::ArchiveOpenProbeStatus::kNotArchive) {
      openwow::core::StormSetLastError(static_cast<int>(mpq::kStormErrorNotArchive));
      return false;
    }
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  const std::string native_path_string = native_path.string();
  void *archive = nullptr;
  if (!mpq::OpenStormArchive(path ? native_path_string.c_str() : nullptr, priority, flags,
                             &archive)) {
    openwow::core::StormSetLastError(static_cast<int>(openwow::platform::GetPlatformLastError()));
    return false;
  }
  *out_handle = archive;
  return true;
#else
  (void)path;
  (void)priority;
  (void)flags;
  (void)out_handle;
  return false;
#endif
}

bool ArchiveRegistry::CloseRawArchive(void *archive) { return impl_->CloseRaw(archive); }

bool ArchiveRegistry::OpenRawArchiveFile(void *archive, const char *filename, void **out_file,
                                         std::uint32_t *out_size,
                                         std::string *out_archive_path,
                                         std::shared_ptr<void> *out_retained_archive) {
  if (!archive || !filename || !*filename || !out_file || !out_size) {
    return false;
  }
  *out_file = nullptr;
  *out_size = 0;
  if (out_archive_path) {
    out_archive_path->clear();
  }
  if (out_retained_archive) {
    out_retained_archive->reset();
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  auto retained_archive = impl_->RetainRaw(archive);
  void *file = nullptr;
  if (!TryOpenArchiveFileHandle(archive, filename, &file)) {
    return false;
  }
  DWORD high = 0;
  const DWORD low = SFileGetFileSize(static_cast<HANDLE>(file), &high);
  if (low == SFILE_INVALID_SIZE || high != 0) {
    const int error = static_cast<int>(openwow::platform::GetPlatformLastError());
    SFileCloseFile(static_cast<HANDLE>(file));
    openwow::core::StormSetLastError(error);
    return false;
  }
  *out_file = file;
  *out_size = low;
  if (out_archive_path) {
    (void)QueryRawArchivePath(archive, out_archive_path);
  }
  if (out_retained_archive) {
    *out_retained_archive = std::move(retained_archive);
  }
  return true;
#else
  (void)archive;
  (void)filename;
  (void)out_file;
  (void)out_size;
  (void)out_archive_path;
  (void)out_retained_archive;
  return false;
#endif
}

std::uint32_t ArchiveRegistry::RegisterMpq(void *raw_archive, const char *path,
                                           std::uint32_t flags, std::int32_t priority) {
  const std::string source_path = path ? path : "";
  return impl_->Register({.kind = ArchiveKind::kMpq,
                          .raw_handle = raw_archive,
                          .source_path = source_path,
                          .patch_resolution_path = ResolveNativePath(source_path),
                          .base_archive_path = source_path,
                          .open_flags = flags,
                          .priority = priority});
}

std::uint32_t ArchiveRegistry::RegisterDirectory(std::string path, std::uint32_t flags,
                                                 std::int32_t priority) {
  return impl_->Register({.kind = ArchiveKind::kDirectory,
                          .source_path = std::move(path),
                          .open_flags = flags,
                          .priority = priority});
}

bool ArchiveRegistry::Close(std::uint32_t token) {

  auto archive = impl_->Unregister(token);
  if (!archive) {
    return false;
  }
  return archive.use_count() == 1 ? archive->Close() : true;
}

bool ArchiveRegistry::Contains(std::uint32_t token) const {
  return static_cast<bool>(impl_->Resolve(token));
}

bool ArchiveRegistry::CopyPath(std::uint32_t token, char *output, int output_capacity) const {
  if (!output || output_capacity <= 0) {
    return false;
  }
  output[0] = '\0';
  const auto access = impl_->Acquire(impl_->Resolve(token), false);
  if (!access) {
    return false;
  }
  openwow::core::SStrCopy(output, access.descriptor().source_path.c_str(),
                          static_cast<std::size_t>(output_capacity));
  return true;
}

ArchiveRegistry::PatchOpenResult ArchiveRegistry::OpenPatch(std::uint32_t base_token,
                                                            const char *path,
                                                            std::int32_t priority,
                                                            const std::function<void()> &before_mpq_open,
                                                            const std::function<bool(
                                                                const std::string &, std::uint32_t)> &
                                                                open_directory) {
  if (!path || !*path) {
    return {};
  }
  const auto base = impl_->Acquire(impl_->Resolve(base_token), true);
  if (!base) {
    return {};
  }
  if (base.descriptor().kind == ArchiveKind::kDirectory) {
    if (!open_directory) {
      return {};
    }
    std::array<char, 260> patched_path{};
    openwow::core::JoinStormPathBounded(patched_path.data(), static_cast<int>(patched_path.size()),
                                        base.descriptor().source_path.c_str(), path);
    const std::string directory_path = patched_path.data();
    return {.base_kind = PatchBaseKind::kDirectory,
            .directory_open_result = open_directory(directory_path,
                                                     base.descriptor().open_flags)};
  }
  if (before_mpq_open) {
    before_mpq_open();
  }
  if (base.descriptor().base_archive_path.empty()) {
    return {};
  }
  const auto resolved_patch = ResolvePatchedArchivePath(base.descriptor(), path);
  if (!resolved_patch) {
    return {};
  }
  void *raw_archive = nullptr;
  if (!OpenRawArchive(base.descriptor().base_archive_path.c_str(), priority,
                      base.descriptor().open_flags, &raw_archive)) {
    return {};
  }
  for (const auto &existing_patch : base.descriptor().applied_patch_paths) {
    if (!impl_->ApplyPatch(raw_archive, existing_patch)) {
      const int error = openwow::core::GetStormLastError();
      impl_->CloseRaw(raw_archive);
      openwow::core::StormSetLastError(error);
      return {};
    }
  }
  if (!impl_->ApplyPatch(raw_archive, *resolved_patch)) {
    const int error = openwow::core::GetStormLastError();
    impl_->CloseRaw(raw_archive);
    openwow::core::StormSetLastError(error);
    return {};
  }
  auto patches = base.descriptor().applied_patch_paths;
  patches.push_back(*resolved_patch);
  const std::uint32_t token = impl_->Register({.kind = ArchiveKind::kMpq,
                                                .raw_handle = raw_archive,
                                                .source_path = base.descriptor().source_path,
                                                 .patch_resolution_path = *resolved_patch,
                                                 .base_archive_path = base.descriptor().base_archive_path,
                                                 .applied_patch_paths = std::move(patches),
                                                 .open_flags = base.descriptor().open_flags,
                                                 .priority = priority});
  return {.base_kind = PatchBaseKind::kMpq, .archive_token = token};
}

bool ArchiveRegistry::Authenticate(std::uint32_t token, std::int32_t *out_result,
                                   const void *modulus, int modulus_size, const void *exponent,
                                   int exponent_size, const char *suffix) {
  constexpr int kAuthenticated = 5;
  constexpr int kNotAuthenticated = 1;
  constexpr int kStormInvalidHandle = 6;
  constexpr int kStormNotAuthenticated = 1244;
  std::int32_t local_result = 0;
  if (!out_result) {
    out_result = &local_result;
  }
  *out_result = 0;
  const auto access = impl_->Acquire(impl_->Resolve(token), true);
  if (!access || access.descriptor().kind != ArchiveKind::kMpq || !access.raw_handle()) {
    openwow::core::StormSetLastError(kStormInvalidHandle);
    return false;
  }
  const auto key = ResolveAuthenticationKey(modulus, modulus_size, exponent, exponent_size);
  switch (VerifyAuthenticationSignature(access.descriptor().source_path, access.raw_handle(), key,
                                        suffix)) {
  case AuthenticationOutcome::kVerified:
    *out_result = kAuthenticated;
    return true;
  case AuthenticationOutcome::kNotAuthenticated:
    *out_result = kNotAuthenticated;
    openwow::core::StormSetLastError(kStormNotAuthenticated);
    return false;
  case AuthenticationOutcome::kTrailerUnavailable:
  case AuthenticationOutcome::kTransportFailure:
    return false;
  }
  return false;
}

SFileArchiveLookupResult ArchiveRegistry::Lookup(
    std::optional<std::uint32_t> token, const char *filename,
    const DirectoryMemberResolver &resolver, SFileArchiveLookupInfo *out_info) {
  SFileArchiveLookupInfo local_info;
  if (out_info) {
    *out_info = {};
  } else {
    out_info = &local_info;
  }
  if (!filename || !*filename) {
    return SFileArchiveLookupResult::kMiss;
  }
  if (token) {
    const auto access = impl_->Acquire(impl_->Resolve(*token), true);
    return access ? impl_->LookupAccess(access, filename, *token, resolver, out_info)
                  : SFileArchiveLookupResult::kMiss;
  }

  std::unordered_set<void *> seen;
  for (const auto &entry : impl_->Snapshot()) {
    const auto access = impl_->Acquire(entry.handle, true);
    if (!access) {
      continue;
    }
    if (access.raw_handle()) {
      seen.insert(access.raw_handle());
    }
    const auto result = impl_->LookupAccess(access, filename, entry.token, resolver, out_info);
    if (result != SFileArchiveLookupResult::kMiss) {
      return result;
    }
  }
  const auto *slots = openwow::data::GetArchiveSlots();
  if (!slots) {
    return SFileArchiveLookupResult::kMiss;
  }
  for (std::size_t index = 0; index < openwow::data::GetArchiveHandleCount(); ++index) {
    void *raw_archive = slots[index];
    if (!raw_archive || seen.contains(raw_archive)) {
      continue;
    }
    SFileArchiveLookupResult result = SFileArchiveLookupResult::kMiss;
    if (impl_->PopulateMpqLookup(raw_archive, filename, nullptr, 0, out_info, &result)) {
      return result;
    }
  }
  return SFileArchiveLookupResult::kMiss;
}

bool ArchiveRegistry::QueryFileMetadata(const char *filename,
                                        const DirectoryMemberResolver &resolver,
                                        std::string *archive_path, std::uint64_t *block_offset,
                                        std::uint32_t *compressed_size,
                                        std::uint32_t *file_flags) {
  if (!filename || !*filename) {
    return false;
  }
  SFileArchiveLookupInfo info;
  for (const auto &entry : impl_->Snapshot()) {
    const auto access = impl_->Acquire(entry.handle, true);
    if (!access) {
      continue;
    }
    const auto result = impl_->LookupAccess(access, filename, entry.token, resolver, &info);
    if (result == SFileArchiveLookupResult::kMiss) {
      continue;
    }
    if (result != SFileArchiveLookupResult::kArchive) {
      return false;
    }
    if (archive_path) *archive_path = info.archive_path;
    if (block_offset) *block_offset = info.file_position;
    if (compressed_size) *compressed_size = info.compressed_size;
    if (file_flags) *file_flags = info.file_flags;
    return true;
  }
  return false;
}

bool ArchiveRegistry::ReadFileBytes(const char *filename, const DirectoryMemberResolver &resolver,
                                    std::vector<std::uint8_t> *out_bytes) {
  if (!filename || !*filename || !out_bytes) {
    return false;
  }
  for (const auto &entry : impl_->Snapshot()) {
    const auto access = impl_->Acquire(entry.handle, true);
    if (!access) {
      continue;
    }
    if (access.descriptor().kind == ArchiveKind::kDirectory) {
      if (!resolver) continue;
      const auto native_path = resolver(access.descriptor().source_path, filename);
      if (!native_path) continue;
      std::ifstream input(*native_path, std::ios::binary);
      if (!input) continue;
      const std::string content((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>{});
      if (input.bad()) continue;
      out_bytes->assign(content.begin(), content.end());
      return true;
    }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    HANDLE file = nullptr;
    if (!access.raw_handle() ||
        !TryOpenArchiveFileHandle(access.raw_handle(), filename,
                                  reinterpret_cast<void **>(&file))) {
      continue;
    }
    DWORD high = 0;
    const DWORD low = SFileGetFileSize(file, &high);
    if (low == SFILE_INVALID_SIZE || high != 0) {
      const int error = static_cast<int>(openwow::platform::GetPlatformLastError());
      SFileCloseFile(file);
      openwow::core::StormSetLastError(error);
      continue;
    }
    std::vector<std::uint8_t> contents(low);
    DWORD read = 0;
    const bool ok = low == 0 || SFileReadFile(file, contents.data(), low, &read, nullptr) != 0;
    const int error = ok && read == low
                          ? 0
                          : static_cast<int>(openwow::platform::GetPlatformLastError());
    SFileCloseFile(file);
    if (!ok || read != low) {
      openwow::core::StormSetLastError(error);
      continue;
    }
    *out_bytes = std::move(contents);
    return true;
#endif
  }
  return false;
}

std::optional<std::string> ArchiveRegistry::ReadFile(
    std::uint32_t token, const char *filename, const DirectoryMemberResolver &resolver) {
  if (!filename || !*filename) {
    return std::nullopt;
  }
  const auto access = impl_->Acquire(impl_->Resolve(token), true);
  if (!access) {
    return std::nullopt;
  }
  if (access.descriptor().kind == ArchiveKind::kDirectory) {
    if (!resolver) return std::nullopt;
    const auto native_path = resolver(access.descriptor().source_path, filename);
    if (!native_path) return std::nullopt;
    std::ifstream input(*native_path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>{});
    return input.bad() ? std::nullopt
                       : std::optional<std::string>(std::move(content));
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (!access.raw_handle()) {
    return std::nullopt;
  }
  HANDLE file = nullptr;
  if (!TryOpenArchiveFileHandle(access.raw_handle(), filename,
                                reinterpret_cast<void **>(&file))) {
    return std::nullopt;
  }
  DWORD high = 0;
  const DWORD low = SFileGetFileSize(file, &high);
  if (low == SFILE_INVALID_SIZE || high != 0) {
    const int error = static_cast<int>(openwow::platform::GetPlatformLastError());
    SFileCloseFile(file);
    openwow::core::StormSetLastError(error);
    return std::nullopt;
  }
  std::string content(low, '\0');
  DWORD read = 0;
  const bool ok = low == 0 || SFileReadFile(file, content.data(), low, &read, nullptr);
  const int error = ok && read == low
                        ? 0
                        : static_cast<int>(openwow::platform::GetPlatformLastError());
  SFileCloseFile(file);
  if (!ok || read != low) {
    openwow::core::StormSetLastError(error);
    return std::nullopt;
  }
  return content;
#else
  return std::nullopt;
#endif
}

bool ArchiveRegistry::ReadFileBySourcePath(const char *archive_path, const char *filename,
                                           const DirectoryMemberResolver &resolver,
                                           std::optional<std::string> *out_contents) {
  if (!archive_path || !*archive_path || !filename || !out_contents) {
    return false;
  }
  std::uint32_t matched_token = 0;
  {
    std::lock_guard lock(impl_->mutex);
    for (const auto &[token, archive] : impl_->handles) {
      if (archive && !archive->descriptor.source_path.empty() &&
          openwow::text::EqualsIgnoreCaseAscii(archive->descriptor.source_path.c_str(),
                                               archive_path) &&
          (matched_token == 0 || token < matched_token)) {
        matched_token = token;
      }
    }
  }
  if (!matched_token) {
    return false;
  }
  *out_contents = ReadFile(matched_token, filename, resolver);
  return true;
}

std::optional<void *> ArchiveRegistry::FindRawArchiveByPath(const std::string &archive_path) const {
  if (archive_path.empty()) return std::nullopt;
  const auto *slots = openwow::data::GetArchiveSlots();
  if (!slots) return std::nullopt;
  for (std::size_t index = 0; index < openwow::data::GetArchiveHandleCount(); ++index) {
    void *raw_archive = slots[index];
    std::string path;
    if (raw_archive && QueryRawArchivePath(raw_archive, &path) &&
        openwow::text::EqualsIgnoreCaseAscii(path.c_str(), archive_path.c_str())) {
      return raw_archive;
    }
  }
  return std::nullopt;
}

bool ArchiveRegistry::VisitRawArchive(std::uint32_t token, bool lock_operation,
                                      const RawArchiveVisitor &visitor) const {
  if (!visitor) return false;
  const auto access = impl_->Acquire(impl_->Resolve(token), lock_operation);
  if (!access || !access.raw_handle()) return false;
  visitor(access.raw_handle(), access.descriptor().source_path);
  return true;
}

bool ArchiveRegistry::VisitArchive(std::uint32_t token, bool lock_operation,
                                   const RawArchiveVisitor &visitor) const {
  if (!visitor) return false;
  const auto access = impl_->Acquire(impl_->Resolve(token), lock_operation);
  if (!access) return false;
  visitor(access.raw_handle(), access.descriptor().source_path);
  return true;
}

void ArchiveRegistry::ResetForTests() {
  std::unordered_map<std::uint32_t, ArchiveHandle> handles;
  {
    std::lock_guard lock(impl_->mutex);
    handles.swap(impl_->handles);
    impl_->order.clear();
    impl_->next_token = 1;
    impl_->next_open_sequence_raw = 0;
  }
  handles.clear();
}

std::size_t ArchiveRegistry::SizeForTests() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->handles.size();
}

std::vector<std::uint32_t> ArchiveRegistry::TokensInStockOrderForTests() const {
  std::lock_guard lock(impl_->mutex);
  std::vector<std::uint32_t> tokens;
  tokens.reserve(impl_->order.size());
  for (const auto &[_, token] : impl_->order) tokens.push_back(token);
  return tokens;
}

void ArchiveRegistry::SetRawOpenHookForTests(RawOpenHook hook) {
  impl_->raw_open_hook = std::move(hook);
}
void ArchiveRegistry::SetRawPatchHookForTests(RawPatchHook hook) {
  impl_->raw_patch_hook = std::move(hook);
}
void ArchiveRegistry::SetRawCloseHookForTests(RawCloseHook hook) {
  impl_->close_lifetime->raw_close_hook = std::move(hook);
}
void ArchiveRegistry::ResetRawHooksForTests() {
  impl_->raw_open_hook = {};
  impl_->raw_patch_hook = {};
  impl_->close_lifetime->raw_close_hook = {};
}

}

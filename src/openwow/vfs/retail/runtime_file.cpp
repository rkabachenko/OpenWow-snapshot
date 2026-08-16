#include "openwow/vfs/retail/runtime_file.h"

#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_file_io.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_tls.h"
#include "openwow/data/streaming_init.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/runtime/time/game_time.h"
#include "openwow/vfs/retail/archive_registry.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <unordered_map>

#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
#include <StormLib.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace openwow::vfs {
namespace {

constexpr std::uint16_t kReadOnly = 0x0001u;
constexpr std::uint16_t kHidden = 0x0002u;
constexpr std::uint16_t kSystem = 0x0004u;
constexpr std::uint16_t kDirectory = 0x0010u;
constexpr std::uint16_t kArchive = 0x0020u;
constexpr std::uint16_t kNormal = 0x0080u;
constexpr std::uint16_t kTemporary = 0x0100u;
constexpr std::uint16_t kSettable =
    kReadOnly | kHidden | kSystem | kArchive | kNormal | kTemporary;
constexpr std::uint32_t kNonDirectoryTranslated = 0x0020u;
constexpr std::uint32_t kDirectoryTranslated = 0x0040u;

struct LooseMetadataState {
  std::mutex mutex;
  std::unordered_map<std::string, std::uint16_t> attributes;
};

LooseMetadataState &MetadataState() {
  static LooseMetadataState state;
  return state;
}

std::string NormalizeMetadataKey(std::filesystem::path path) {
  std::error_code ec;
  if (const auto canonical = std::filesystem::weakly_canonical(path, ec); !ec) {
    path = canonical;
  } else if (!path.is_absolute()) {
    const auto cwd = std::filesystem::current_path(ec);
    path = ec ? path.lexically_normal() : (cwd / path).lexically_normal();
  } else {
    path = path.lexically_normal();
  }

  std::string key = path.generic_string();
  std::replace(key.begin(), key.end(), '/', '\\');
  for (char &value : key) {
    if (value >= 'A' && value <= 'Z') {
      value = static_cast<char>(value + ('a' - 'A'));
    }
  }
  return key;
}

std::optional<std::uint16_t> LookupAttributes(const std::filesystem::path &path) {
  auto &state = MetadataState();
  std::lock_guard lock(state.mutex);
  const auto it = state.attributes.find(NormalizeMetadataKey(path));
  return it == state.attributes.end() ? std::nullopt
                                      : std::optional<std::uint16_t>{it->second};
}

std::int64_t FileTimeTicksToNsSince2000(const std::uint64_t ticks) {
  return openwow::core::ida::TimeNsSince2000FromFileTimeTicks(ticks);
}

#if !defined(_WIN32)
std::int64_t PosixTimeToNsSince2000(const std::int64_t seconds,
                                    const std::int64_t nanoseconds) {
  constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;
  constexpr std::int64_t kFileTimeTicksPerSecond = 10'000'000LL;
  constexpr std::uint64_t kUnixEpochFileTimeOffsetTicks = 116444736000000000ULL;
  const auto clamped = std::clamp<std::int64_t>(nanoseconds, 0, kNanosecondsPerSecond - 1);
  return FileTimeTicksToNsSince2000(
      static_cast<std::uint64_t>(seconds * kFileTimeTicksPerSecond + clamped / 100LL +
                                 static_cast<std::int64_t>(kUnixEpochFileTimeOffsetTicks)));
}
#endif

std::uint32_t TranslateAttributes(const std::uint16_t attributes) {
  std::uint32_t translated = 0;
  translated |= (attributes & kReadOnly) != 0 ? 0x0001u : 0u;
  translated |= (attributes & kHidden) != 0 ? 0x0002u : 0u;
  translated |= (attributes & kSystem) != 0 ? 0x0004u : 0u;
  translated |= (attributes & kArchive) != 0 ? 0x0008u : 0u;
  translated |= (attributes & kTemporary) != 0 ? 0x0010u : 0u;
  translated |= (attributes & kDirectory) != 0 ? kDirectoryTranslated : 0u;
  if ((attributes & kNormal) != 0 || (attributes & kDirectory) == 0) {
    translated |= kNonDirectoryTranslated;
  }
  return translated;
}

openwow::core::TlsSlotHandle g_fatal_read_slot = openwow::core::kInvalidTlsSlot;

void DestroyFatalReadStorage(void *storage) {
  openwow::core::FrameXML_OperatorDelete(storage);
}

void *CreateFatalReadStorage() {
  return new (std::nothrow) std::uint8_t(1);
}

std::int64_t AddCursorOffsetOrCurrent(const std::int64_t base, const std::int32_t offset,
                                      const std::int64_t current) {
  if (offset > 0 && base > std::numeric_limits<std::int64_t>::max() - offset) {
    return current;
  }
  if (offset < 0 && base < -static_cast<std::int64_t>(offset)) {
    return current;
  }
  return base + offset;
}

class FileLockScope {
public:
  explicit FileLockScope(openwow::platform::StormCriticalSection *lock) : lock_(lock) {
    lock_->Enter();
  }
  ~FileLockScope() { lock_->Leave(); }

private:
  openwow::platform::StormCriticalSection *lock_;
};

bool SeekArchive(RuntimeFile &file, const std::uint64_t offset) {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (file.archive_file_handle == nullptr) {
    return false;
  }
  LONG high = static_cast<LONG>(offset >> 32);
  const DWORD low = SFileSetFilePointer(static_cast<HANDLE>(file.archive_file_handle),
                                         static_cast<LONG>(offset & 0xFFFFFFFFu), &high,
                                         FILE_BEGIN);
  const std::uint64_t actual =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32) | low;
  if (actual != offset) {
    openwow::core::StormSetLastError(static_cast<int>(openwow::platform::GetPlatformLastError()));
    return false;
  }
  return true;
#else
  (void)file;
  (void)offset;
  return false;
#endif
}

std::string ReadStatusPath(const RuntimeFile &file) {
  if (!file.native_path.empty()) {
    std::array<char, 1024> bounded{};
    openwow::core::CopyStormPath(bounded.data(), file.native_path.c_str(),
                                 static_cast<int>(bounded.size()));
    return bounded.data();
  }
  return file.logical_path;
}

bool ReadLocked(RuntimeFile &file, void *buffer, const std::uint64_t offset,
                std::uint32_t *inout_bytes, const bool require_exact,
                const RuntimeFile::StreamingReadDelegate &streaming_read) {
  if (!inout_bytes) {
    return false;
  }
  if (file.streaming_part_backing.has_value()) {
    return streaming_read && streaming_read(buffer, offset, inout_bytes, require_exact);
  }

  const std::uint32_t requested = *inout_bytes;
  *inout_bytes = 0;
  if (requested == 0) {
    return true;
  }
  if (file.buffered_source) {
    if (require_exact &&
        (offset > file.buffered_bytes.size() ||
         file.buffered_bytes.size() - static_cast<std::size_t>(offset) < requested)) {
      return false;
    }
    if (offset >= file.buffered_bytes.size()) {
      return !require_exact;
    }
    const auto available = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        requested, file.buffered_bytes.size() - static_cast<std::size_t>(offset)));
    if (available != 0) {
      std::memcpy(buffer, file.buffered_bytes.data() + static_cast<std::size_t>(offset), available);
    }
    *inout_bytes = available;
    return !require_exact || available == requested;
  }
  if (file.file_io) {
    if (require_exact) {
      if (!file.file_io->Read(buffer, static_cast<std::int64_t>(offset), requested, nullptr)) {
        return false;
      }
      *inout_bytes = requested;
      return true;
    }
    std::uint32_t actual = 0;
    if (!file.file_io->ReadAllowShort(buffer, static_cast<std::int64_t>(offset), requested,
                                      &actual)) {
      openwow::data::SetCurrentStreamingStatusCode(5);
      openwow::data::PushStreamingStatusMessage("Win32 Read - " + ReadStatusPath(file), 5, 1);
      return false;
    }
    *inout_bytes = actual;
    return true;
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (file.archive_file_handle == nullptr || !SeekArchive(file, offset)) {
    if (file.archive_file_handle == nullptr && !require_exact) {
      openwow::data::SetCurrentStreamingStatusCode(8);
    }
    return false;
  }
  DWORD actual = 0;
  const bool ok = SFileReadFile(static_cast<HANDLE>(file.archive_file_handle), buffer, requested,
                                &actual, nullptr) != 0;
  if (!ok || (require_exact && actual != requested)) {
    openwow::core::StormSetLastError(static_cast<int>(openwow::platform::GetPlatformLastError()));
    return false;
  }
  *inout_bytes = require_exact ? requested : actual;
  return true;
#else
  if (!require_exact) {
    openwow::data::SetCurrentStreamingStatusCode(8);
  }
  return false;
#endif
}

bool WriteLocked(RuntimeFile &file, const void *buffer, const std::uint64_t offset,
                 std::uint32_t *inout_bytes) {
  if (!inout_bytes) {
    return false;
  }
  if (!file.file_io) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }
  const std::uint32_t requested = *inout_bytes;
  if (requested == 0) {
    return true;
  }
  if (!file.file_io->Write(buffer, static_cast<std::int64_t>(offset), requested)) {
    openwow::data::SetCurrentStreamingStatusCode(6);
    openwow::data::PushStreamingStatusMessage("Win32 Write - " + ReadStatusPath(file), 6, 1);
    return false;
  }
  std::uint64_t updated = std::max(file.size, offset + requested);
  file.file_io->GetCachedSize(&updated);
  file.size = updated;
  file.SyncSizeFields();
  *inout_bytes = requested;
  return true;
}

}

std::optional<LooseFileMetadataSnapshot> LooseFileMetadataStore::Query(
    const std::string &native_path) {
  if (native_path.empty()) {
    return std::nullopt;
  }

#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA data{};
  const auto path = std::filesystem::path(native_path);
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) == FALSE) {
    return std::nullopt;
  }
  const auto ticks = [](const FILETIME &time) {
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
  };
  return LooseFileMetadataSnapshot{
      .attribute_word = static_cast<std::uint16_t>(data.dwFileAttributes & 0xFFFFu),
      .size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
      .creation_time_ns_since_2000 = FileTimeTicksToNsSince2000(ticks(data.ftCreationTime)),
      .last_access_time_ns_since_2000 = FileTimeTicksToNsSince2000(ticks(data.ftLastAccessTime)),
      .last_write_time_ns_since_2000 = FileTimeTicksToNsSince2000(ticks(data.ftLastWriteTime)),
  };
#else
  LooseFileMetadataSnapshot snapshot;
  std::error_code ec;
  const auto path = std::filesystem::path(native_path);
  const auto status = std::filesystem::status(path, ec);
  if (ec || status.type() == std::filesystem::file_type::none ||
      status.type() == std::filesystem::file_type::not_found) {
    return std::nullopt;
  }
  std::uint16_t attributes = std::filesystem::is_directory(status) ? kDirectory : 0u;
  if (const auto override = LookupAttributes(path); override.has_value()) {
    attributes |= static_cast<std::uint16_t>(*override & ~kDirectory);
  }
  struct stat native_stat {};
  if (::stat(path.c_str(), &native_stat) == 0) {
    if (std::filesystem::is_regular_file(status) && native_stat.st_size > 0) {
      snapshot.size = static_cast<std::uint64_t>(native_stat.st_size);
    }
#if defined(__APPLE__) || defined(__FreeBSD__)
    snapshot.creation_time_ns_since_2000 = PosixTimeToNsSince2000(
        native_stat.st_birthtimespec.tv_sec, native_stat.st_birthtimespec.tv_nsec);
    snapshot.last_access_time_ns_since_2000 = PosixTimeToNsSince2000(
        native_stat.st_atimespec.tv_sec, native_stat.st_atimespec.tv_nsec);
    snapshot.last_write_time_ns_since_2000 = PosixTimeToNsSince2000(
        native_stat.st_mtimespec.tv_sec, native_stat.st_mtimespec.tv_nsec);
#else
    snapshot.last_access_time_ns_since_2000 =
        PosixTimeToNsSince2000(native_stat.st_atim.tv_sec, native_stat.st_atim.tv_nsec);
    snapshot.last_write_time_ns_since_2000 =
        PosixTimeToNsSince2000(native_stat.st_mtim.tv_sec, native_stat.st_mtim.tv_nsec);
#endif
  }
  snapshot.attribute_word = attributes;
  return snapshot;
#endif
}

void LooseFileMetadataStore::StoreAttributes(const std::filesystem::path &path,
                                              const std::uint16_t attributes) {
  auto &state = MetadataState();
  std::lock_guard lock(state.mutex);
  state.attributes[NormalizeMetadataKey(path)] = static_cast<std::uint16_t>(attributes & kSettable);
}

void LooseFileMetadataStore::ResetForTests() {
  auto &state = MetadataState();
  std::lock_guard lock(state.mutex);
  state.attributes.clear();
}

RuntimeFile::RuntimeFile(const int type) {
  auto *lock = new openwow::platform::StormCriticalSection();
  lock->Initialize();
  handle.critical_section = lock;
  handle.type = type;
}

RuntimeFile::~RuntimeFile() {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (archive_file_handle != nullptr) {
    SFileCloseFile(static_cast<HANDLE>(archive_file_handle));
  }
#endif
  if (auto *lock = critical_section(); lock != nullptr) {
    lock->Delete();
    delete lock;
  }
}

bool RuntimeFile::OpenLoose(const char *path, const char *mode) {
  file_io = std::make_unique<openwow::core::StormFileIO>();
  if (!file_io->Open(path, mode) || !file_io->GetCachedSize(&size)) {
    return false;
  }
  native_path = path ? path : "";
  SetCursor(0);
  SyncSizeFields();
  SyncLooseMetadata();
  return true;
}

bool RuntimeFile::OpenArchive(ArchiveRegistry &archives, void *raw_archive,
                              const char *filename) {
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  void *opened_file = nullptr;
  std::uint32_t opened_size = 0;
  std::string queried_path;
  std::string *const path_output = archive_path.empty() ? &queried_path : nullptr;
  std::shared_ptr<void> retained_archive;
  if (!archives.OpenRawArchiveFile(raw_archive, filename, &opened_file, &opened_size, path_output,
                                   &retained_archive)) {
    return false;
  }
  archive_file_handle = opened_file;
  archive_access = std::move(retained_archive);
  size = opened_size;
  native_path.clear();
  if (archive_path.empty() && !queried_path.empty()) {
    archive_path = std::move(queried_path);
  }
  SetCursor(0);
  SyncSizeFields();
  ResetAttributes();
  return true;
#else
  (void)archives;
  (void)raw_archive;
  (void)filename;
  return false;
#endif
}

void RuntimeFile::SyncSizeFields() {
  handle.file_size = static_cast<std::int32_t>(size & 0xFFFFFFFFu);
  handle.field_18 = static_cast<std::int32_t>((size >> 32) & 0xFFFFFFFFu);
}

bool RuntimeFile::RefreshSize(std::uint64_t *out_size) {
  if (!out_size) {
    return false;
  }
  std::uint64_t refreshed = 0;
  if (file_io) {
    if (!file_io->GetSize(&refreshed)) {
      return false;
    }
  }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  else if (archive_file_handle != nullptr) {
    DWORD high = 0;
    const DWORD low = SFileGetFileSize(static_cast<HANDLE>(archive_file_handle), &high);
    if (low == SFILE_INVALID_SIZE) {
      return false;
    }
    refreshed = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32);
  }
#endif
  else {
    return false;
  }
  size = refreshed;
  SyncSizeFields();
  *out_size = refreshed;
  return true;
}

void RuntimeFile::ResetAttributes() {
  translated_attributes = 0;
  creation_time_ns_since_2000 = 0;
  last_access_time_ns_since_2000 = 0;
  last_write_time_ns_since_2000 = 0;
  object_kind = SFileNativeObjectKind::kUnknown;
  non_directory_hint = false;
}

void RuntimeFile::UpdateTranslatedAttributes(const std::uint32_t attributes) {
  translated_attributes = attributes;
  non_directory_hint = (attributes & kNonDirectoryTranslated) != 0;
  object_kind = (attributes & kDirectoryTranslated) != 0
                    ? SFileNativeObjectKind::kDirectory
                    : non_directory_hint ? SFileNativeObjectKind::kFile
                                         : SFileNativeObjectKind::kUnknown;
}

void RuntimeFile::SyncLooseMetadata() {
  ResetAttributes();
  const auto snapshot = LooseFileMetadataStore::Query(native_path);
  if (!snapshot) {
    return;
  }
  UpdateTranslatedAttributes(TranslateAttributes(snapshot->attribute_word));
  creation_time_ns_since_2000 = snapshot->creation_time_ns_since_2000;
  last_access_time_ns_since_2000 = snapshot->last_access_time_ns_since_2000;
  last_write_time_ns_since_2000 = snapshot->last_write_time_ns_since_2000;
}

void RuntimeFile::SetCursor(const std::int64_t new_position) {
  position = new_position;
  handle.field_1C = static_cast<std::int32_t>(position);
}

bool RuntimeFile::TryCursorOffset(std::uint64_t *out_offset) const {
  if (!out_offset || position < 0) {
    return false;
  }
  *out_offset = static_cast<std::uint64_t>(position);
  return true;
}

std::int64_t RuntimeFile::SignedSize() const {
  return size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
             ? std::numeric_limits<std::int64_t>::max()
             : static_cast<std::int64_t>(size);
}

std::int64_t RuntimeFile::ApplyNativeCursorMove(const std::int32_t offset,
                                                const std::uint32_t method) {
  const auto current = position;
  if (method == 0) {
    return offset < 0 ? current : offset;
  }
  if (method == 1) {
    return AddCursorOffsetOrCurrent(current, offset, current);
  }
  if (method == 2) {
    std::uint64_t refreshed = 0;
    return RefreshSize(&refreshed) ? AddCursorOffsetOrCurrent(SignedSize(), offset, current)
                                   : current;
  }
  return current;
}

std::int64_t RuntimeFile::ApplyArchiveCursorMove(const std::int32_t offset,
                                                 const std::uint32_t method) {
  const auto current = static_cast<std::int32_t>(position);
  if (method == 0) {
    return offset;
  }
  if (method == 1) {
    if (offset < 0 && current >= 0 &&
        static_cast<std::uint32_t>(current) < static_cast<std::uint32_t>(-offset)) {
      return 0;
    }
    return static_cast<std::int32_t>(current + offset);
  }
  if (method == 2) {
    std::uint64_t refreshed = 0;
    if (!RefreshSize(&refreshed)) {
      return current;
    }
    if (offset < 0 && refreshed < static_cast<std::uint64_t>(-static_cast<std::int64_t>(offset))) {
      return 0;
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(refreshed) +
                                     static_cast<std::uint32_t>(offset));
  }
  return current;
}

std::int64_t RuntimeFile::ApplyBufferedCursorMove(const std::int32_t offset,
                                                  const std::uint32_t method) const {
  const auto current = static_cast<std::int32_t>(position);
  if (method == 0) {
    return offset;
  }
  if (method == 1) {
    return static_cast<std::int32_t>(current + offset);
  }
  return method == 2 ? static_cast<std::int32_t>(handle.file_size - offset) : current;
}

openwow::platform::StormCriticalSection *RuntimeFile::critical_section() const {
  return static_cast<openwow::platform::StormCriticalSection *>(handle.critical_section);
}

bool RuntimeFile::SetBackingCursor(const std::uint64_t offset) {
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return false;
  }
  FileLockScope lock(critical_section());
  const auto signed_offset = static_cast<std::int64_t>(offset);
  if (file_io) {
    if (!file_io->Seek(signed_offset)) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      return false;
    }
  } else if (archive_file_handle != nullptr && !SeekArchive(*this, offset)) {
    return false;
  }
  SetCursor(signed_offset);
  return true;
}

bool RuntimeFile::ReadAtOffset(void *buffer, const std::uint64_t offset,
                               std::uint32_t *inout_bytes, const bool require_exact,
                               const bool preserve_count_on_failure,
                               const StreamingReadDelegate &streaming_read) {
  const std::uint32_t requested = inout_bytes ? *inout_bytes : 0;
  FileLockScope lock(critical_section());
  const std::int64_t saved_position = position;
  const bool ok = ReadLocked(*this, buffer, offset, inout_bytes, require_exact, streaming_read);
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (archive_file_handle != nullptr && saved_position >= 0 &&
      !SeekArchive(*this, static_cast<std::uint64_t>(saved_position))) {
    if (inout_bytes) {
      *inout_bytes = preserve_count_on_failure ? requested : 0;
    }
    return false;
  }
#endif
  if (!ok && inout_bytes) {
    *inout_bytes = preserve_count_on_failure ? requested : 0;
  }
  return ok;
}

bool RuntimeFile::ReadCurrent(void *buffer, std::uint32_t *inout_bytes,
                              const bool require_exact,
                              const StreamingReadDelegate &streaming_read) {
  FileLockScope lock(critical_section());
  std::uint64_t offset = 0;
  if (!TryCursorOffset(&offset)) {
    return false;
  }
  const bool ok = ReadLocked(*this, buffer, offset, inout_bytes, require_exact, streaming_read);
  if (ok) {
    SetCursor(static_cast<std::int64_t>(offset + *inout_bytes));
  }
  return ok;
}

bool RuntimeFile::ReadArchiveCurrent(void *buffer, const std::uint32_t requested_bytes,
                                     std::uint32_t *out_bytes_read,
                                     const StreamingReadDelegate &streaming_read,
                                     const DirectReadFallback &direct_fallback) {
  if (!out_bytes_read) {
    return false;
  }
  FileLockScope lock(critical_section());
  std::uint64_t offset = 0;
  if (!TryCursorOffset(&offset)) {
    return false;
  }
  std::uint32_t actual = requested_bytes;
  bool ok = ReadLocked(*this, buffer, offset, &actual, false, streaming_read);
  if (!ok) {
    actual = requested_bytes;
    ok = direct_fallback && direct_fallback(offset, buffer, &actual);
  }
  SetCursor(static_cast<std::int64_t>(offset + actual));
  *out_bytes_read = actual;
  if (actual != requested_bytes) {
    openwow::core::StormSetLastError(38);
  }
  return ok;
}

bool RuntimeFile::WriteCurrent(const void *buffer, const std::uint32_t bytes_to_write) {
  FileLockScope lock(critical_section());
  std::uint64_t offset = 0;
  if (!TryCursorOffset(&offset)) {
    return false;
  }
  std::uint32_t written = bytes_to_write;
  if (!WriteLocked(*this, buffer, offset, &written)) {
    return false;
  }
  SetCursor(position + static_cast<std::int64_t>(written));
  return true;
}

bool RuntimeFile::WriteAtOffset(const void *buffer, const std::uint64_t offset,
                                std::uint32_t *inout_bytes) {
  FileLockScope lock(critical_section());
  if (!inout_bytes || !file_io) {
    if (!file_io) {
      openwow::data::SetCurrentStreamingStatusCode(8);
    }
    return false;
  }
  if (*inout_bytes == 0) {
    return true;
  }
  std::int64_t saved_native_position = 0;
  if (!file_io->QueryPosition(&saved_native_position)) {
    return false;
  }
  const bool write_ok = WriteLocked(*this, buffer, offset, inout_bytes);
  const bool restore_ok = file_io->Seek(saved_native_position);
  return write_ok && restore_ok;
}

bool RuntimeFile::Flush() {
  FileLockScope lock(critical_section());
  return file_io && file_io->Flush();
}

bool RuntimeFile::Resize(const std::uint64_t new_size) {
  FileLockScope lock(critical_section());
  if (!file_io) {
    return false;
  }
  std::int64_t saved_native_position = 0;
  if (!file_io->QueryPosition(&saved_native_position)) {
    return false;
  }
  const bool truncate_ok = file_io->Truncate(static_cast<std::int64_t>(new_size));
  (void)file_io->Seek(saved_native_position);
  if (!truncate_ok) {
    return false;
  }
  size = new_size;
  SyncSizeFields();
  return true;
}

bool RuntimeFile::Buffer(const StreamingReadDelegate &streaming_read) {
  FileLockScope lock(critical_section());
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  std::size_t copied = 0;
  while (copied < bytes.size()) {
    std::uint32_t chunk = static_cast<std::uint32_t>(std::min<std::size_t>(
        bytes.size() - copied, std::numeric_limits<std::uint32_t>::max()));
    if (!ReadLocked(*this, bytes.data() + copied, copied, &chunk, true, streaming_read) ||
        chunk == 0) {
      return false;
    }
    copied += chunk;
  }
  file_io.reset();
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
  if (archive_file_handle != nullptr) {
    SFileCloseFile(static_cast<HANDLE>(archive_file_handle));
    archive_file_handle = nullptr;
  }
#endif
  buffered_source = true;
  buffered_bytes = std::move(bytes);
  handle.type = 5;
  SetCursor(0);
  return true;
}

bool RuntimeFile::QueryNativePosition(std::int64_t *out_position) {
  FileLockScope lock(critical_section());
  return file_io && file_io->QueryPosition(out_position);
}

bool RuntimeFile::SetLastWriteTime(const std::int64_t time_ns_since_2000) {
  FileLockScope lock(critical_section());
  if (!file_io || !file_io->SetLastWriteTimeNsSince2000(time_ns_since_2000)) {
    return false;
  }
  last_write_time_ns_since_2000 = time_ns_since_2000;
  return true;
}

RuntimeSFileHandleMetadata RuntimeFile::Metadata() const {
  return {
      .native_path = native_path,
      .archive_path = archive_path,
      .size = size,
      .translated_attributes = translated_attributes,
      .creation_time_ns_since_2000 = creation_time_ns_since_2000,
      .last_access_time_ns_since_2000 = last_access_time_ns_since_2000,
      .last_write_time_ns_since_2000 = last_write_time_ns_since_2000,
      .object_kind = object_kind,
      .non_directory_hint = non_directory_hint,
  };
}

std::uint8_t *RuntimeFile::FatalReadFlag() {
  return static_cast<std::uint8_t *>(openwow::core::StormTls::Instance().GetOrCreateValue(
      g_fatal_read_slot, CreateFatalReadStorage, DestroyFatalReadStorage));
}

std::uint8_t *RuntimeFile::SetFatalReadFlag(const std::uint8_t value) {
  auto *storage = FatalReadFlag();
  if (storage) {
    *storage = value;
  }
  return storage;
}

void RuntimeFile::ResetFatalReadFlagForTests() {
  auto &tls = openwow::core::StormTls::Instance();
  if (g_fatal_read_slot == openwow::core::kInvalidTlsSlot) {
    return;
  }
  if (auto *storage = static_cast<std::uint8_t *>(tls.GetValue(g_fatal_read_slot)); storage) {
    tls.SetValue(g_fatal_read_slot, nullptr);
    DestroyFatalReadStorage(storage);
  }
  tls.FreeSlot(g_fatal_read_slot);
  g_fatal_read_slot = openwow::core::kInvalidTlsSlot;
}

}

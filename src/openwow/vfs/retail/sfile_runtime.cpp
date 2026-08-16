#include "openwow/vfs/retail/sfile_runtime.h"

#include "openwow/core/io_unit_container.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_utils.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/archive_registry.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/retail_path_resolver.h"
#include "openwow/vfs/retail/sfile_archive.h"
#include "openwow/vfs/retail/sfile_configuration.h"
#include "openwow/vfs/retail/sound_cache/sound_cache.h"
#include "openwow/vfs/retail/streaming/data_preload_controller.h"
#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

namespace openwow::core {
bool IOFileUnit_DirectRead(std::uint32_t file_offset, const char *filepath,
                           void *dest, std::uint32_t size,
                           std::uint32_t *bytes_read);
}

namespace openwow::vfs {
namespace {

std::function<void()> &MutableSFileReadPrologueHookForTests() {
  static std::function<void()> hook;
  return hook;
}

class StormCriticalSectionScope {
public:
  explicit StormCriticalSectionScope(openwow::platform::StormCriticalSection *section)
      : section_(section) {
    section_->Enter();
  }
  ~StormCriticalSectionScope() { section_->Leave(); }

private:
  openwow::platform::StormCriticalSection *section_;
};

bool PathResolvesToNativeFile(const char *path) {
  if (!path || !*path) return false;
  std::error_code ec;
  return std::filesystem::is_regular_file(ToNativePath(path), ec);
}

constexpr int kStormStatusContextReadFailure = 1;
constexpr int kStormStatusErrorSuppressFatalRead = 10;
constexpr int kStormStatusContextNetworkRead = 21;
constexpr int kStormStatusContextSFileReadFailureNode = 4;
constexpr int kSFileReadFailurePathCapacity = 260;

bool ShouldRaiseSFileReadFatal(const RuntimeFile &runtime_file) {
  const auto *fatal_flag = RuntimeFile::FatalReadFlag();
  return (runtime_file.handle.type == 3 || runtime_file.handle.type == 4) && fatal_flag &&
         *fatal_flag != 0 &&
         !openwow::data::StreamingStatusContainsContextAndError(kStormStatusContextReadFailure,
                                                                 kStormStatusErrorSuppressFatalRead);
}

std::string FormatPlatformReadErrorText() {
  const auto platform_error = openwow::platform::GetPlatformLastError();
  return platform_error == 0
             ? std::string{}
             : std::error_code(static_cast<int>(platform_error), std::system_category()).message();
}

std::string BuildBoundedSFileReadFailurePath(const char *path) {
  std::array<char, kSFileReadFailurePathCapacity> bounded_path{};
  openwow::core::CopyStormPath(bounded_path.data(), path ? path : "",
                               static_cast<int>(bounded_path.size()));
  auto &storage = openwow::core::StreamingStorage::Instance();
  if (!storage.HasManifestEntryForPath(bounded_path.data())) return bounded_path.data();

  std::array<char, kSFileReadFailurePathCapacity> part_path{};
  storage.BuildVariantPath(part_path.data(), static_cast<int>(part_path.size()),
                           bounded_path.data(), "part");
  openwow::core::CopyStormPath(bounded_path.data(), part_path.data(),
                               static_cast<int>(bounded_path.size()));
  return bounded_path.data();
}

std::string BuildSFileReadFailureStatusText(const RuntimeFile &runtime_file) {
  const std::string logical_path = BuildBoundedSFileReadFailurePath(
      runtime_file.logical_path.empty() ? "" : runtime_file.logical_path.c_str());
  if (runtime_file.archive_path.empty()) return "SFileReadFile - " + logical_path;

  std::array<char, kSFileReadFailurePathCapacity> archive_path{};
  openwow::core::CopyStormPath(archive_path.data(), runtime_file.archive_path.c_str(),
                               static_cast<int>(archive_path.size()));
  return std::string("SFileReadFile - ") + archive_path.data() + " - " + logical_path;
}

void PushSFileReadFailureStatusNode(const RuntimeFile &runtime_file) {
  openwow::data::PushStreamingStatusMessage(BuildSFileReadFailureStatusText(runtime_file),
                                             kStormStatusContextSFileReadFailureNode, 0);
}

[[noreturn]] void RaiseSFileReadFatal(const RuntimeFile &runtime_file) {
  const std::string platform_error = FormatPlatformReadErrorText();
  const char *status = GetStreamingStatusMessageText();
  if (openwow::data::StreamingStatusContainsContext(kStormStatusContextNetworkRead)) {
    if (!platform_error.empty()) {
      openwow::core::SErrFatalCondition(
          "Failed to read data from the network. Please check your Internet connection and try "
          "again.\n\nDebug Details:\n\n%sStorm Error Msg:%s",
          status, platform_error.c_str());
    }
    openwow::core::SErrFatalCondition(
        "Failed to read data from the network. Please check your Internet connection and try "
        "again.\n\nDebug Details:\n\n%s",
        status);
  }

  const char *logical_path = runtime_file.logical_path.empty() ? "" : runtime_file.logical_path.c_str();
  if (!platform_error.empty()) {
    openwow::core::SErrFatalCondition(
        "Failed to read file %s.\n\nDebug Details:\n\n%sStorm Error Msg:%s", logical_path,
        status, platform_error.c_str());
  }
  openwow::core::SErrFatalCondition("Failed to read file %s.\n\nDebug Details:\n\n%s",
                                    logical_path, status);
}

bool TryLooseFileReadDirectAtOffsetLocked(const RuntimeFile &runtime_file,
                                           std::uint64_t offset, void *buffer,
                                           std::uint32_t *inout_bytes) {
  if (!inout_bytes || runtime_file.native_path.empty() ||
      offset > std::numeric_limits<std::uint32_t>::max()) return false;
  std::uint32_t direct_bytes = *inout_bytes;
  if (!openwow::core::IOFileUnit_DirectRead(static_cast<std::uint32_t>(offset),
                                            runtime_file.native_path.c_str(), buffer,
                                            direct_bytes, &direct_bytes)) {
    *inout_bytes = 0;
    return false;
  }
  *inout_bytes = direct_bytes;
  return true;
}

bool ReadArchiveHandle(RuntimeFile &runtime_file, void *buffer, std::uint32_t requested_bytes,
                       std::uint32_t *out_bytes_read) {
  const bool read_ok = runtime_file.ReadArchiveCurrent(
      buffer, requested_bytes, out_bytes_read,
      [&runtime_file](void *stream_buffer, std::uint64_t offset, std::uint32_t *bytes, bool exact) {
        return ReadStreamingPartBackingAtOffsetLocked(runtime_file, stream_buffer, offset, bytes,
                                                       exact);
      },
      [&runtime_file](std::uint64_t offset, void *direct_buffer, std::uint32_t *bytes) {
        return TryLooseFileReadDirectAtOffsetLocked(runtime_file, offset, direct_buffer, bytes);
      });
  if (!read_ok) PushSFileReadFailureStatusNode(runtime_file);
  return read_ok;
}

}

SFileHandle *SFileHandle_Init(SFileHandle *handle, int type) {
  if (!handle) return nullptr;
  auto *critical_section = new openwow::platform::StormCriticalSection();
  critical_section->Initialize();
  *handle = {};
  handle->critical_section = critical_section;
  handle->type = type;
  return handle;
}

int SFileOpenFile(void *archive_handle, const char *filename, int flags, int *out_file) {
  openwow::data::ResetCurrentStreamingStatusChain();
  if (!filename || !out_file) return 0;
  *out_file = 0;

  const auto finalize = [&](std::shared_ptr<RuntimeFile> file, int result, bool allow_preload) {
    if (allow_preload && (flags & 0x20000) != 0 &&
        !file->Buffer([&file](void *buffer, std::uint64_t offset, std::uint32_t *bytes, bool exact) {
          return ReadStreamingPartBackingAtOffsetLocked(*file, buffer, offset, bytes, exact);
        })) {
      openwow::core::SErrSetLastError(38);
      return 0;
    }
    *out_file = RetailRuntimeFileRegistry().Store(std::move(file));
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                              std::string("SFileOpenFile: ") + filename);
    return result;
  };

  const auto open_lookup = [&](const void *lookup_handle, SFileArchiveLookupResult result,
                               const SFileArchiveLookupInfo &info) {
    if (result != SFileArchiveLookupResult::kArchive &&
        result != SFileArchiveLookupResult::kDirectoryArchive) {
      openwow::core::SErrSetLastError(2);
      return 0;
    }
    auto file = std::make_shared<RuntimeFile>(3);
    file->logical_path = filename;
    if (result == SFileArchiveLookupResult::kDirectoryArchive) {
      file->archive_path = info.archive_path;
      if (!file->OpenLoose(info.resolved_path.c_str(), "rb")) {
        openwow::core::SErrSetLastError(2);
        return 0;
      }
      return finalize(std::move(file), 1, true);
    }
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    const std::uint32_t token = lookup_handle
                                    ? static_cast<const SArchiveHandle *>(lookup_handle)->archive_token
                                    : info.archive_token;
    if (token != 0) {
      bool opened = false;
      if (!RetailArchiveRegistry().VisitRawArchive(
              token, true, [&](void *raw, const std::string &) {
                file->archive_path = info.archive_path;
                opened = file->OpenArchive(RetailArchiveRegistry(), raw, filename);
              }) ||
          !opened) {
        openwow::core::SErrSetLastError(2);
        return 0;
      }
      return finalize(std::move(file), 1, true);
    }
    if (const auto raw = RetailArchiveRegistry().FindRawArchiveByPath(info.archive_path); raw) {
      file->archive_path = info.archive_path;
      if (file->OpenArchive(RetailArchiveRegistry(), *raw, filename))
        return finalize(std::move(file), 1, true);
    }
#endif
    openwow::core::SErrSetLastError(2);
    return 0;
  };

  if (openwow::data::IsOnlineModeActive() &&
      openwow::core::SFileOpenFile_FindSubstringNoCaseIfNonNull(filename, ".wav", 0x7FFFFFFFu)) {
    std::shared_ptr<RuntimeFile> file;
    if (RetailSoundCache().OpenCachedOrEnqueue(filename, &file))
      return finalize(std::move(file), 1, false);
  }

  if (archive_handle) {
    const auto *archive = static_cast<const SArchiveHandle *>(archive_handle);
    if (archive->type != 0) return 0;
    int result = 0;
    if (!RetailArchiveRegistry().VisitArchive(archive->archive_token, false,
                                               [&](void *, const std::string &) {
      SFileArchiveLookupInfo info;
      const auto lookup = LookupRegisteredArchiveFile(archive_handle, filename, &info);
      result = open_lookup(archive_handle, lookup, info);
    })) return 0;
    return result;
  }

  char resolved_path[260]{};
  std::int32_t type = -1;
  if (!SFileOpenFile_ResolveLoosePath(filename, resolved_path, sizeof(resolved_path),
                                      static_cast<std::uint8_t>(flags), &type)) {
    openwow::core::SErrSetLastError(2);
    return 0;
  }
  if (type == 3) {
    auto file = std::make_shared<RuntimeFile>(type);
    file->logical_path = filename;
#if defined(OPENWOW_HAS_STORMLIB) && OPENWOW_HAS_STORMLIB
    if (!PathResolvesToNativeFile(resolved_path)) {
      SFileArchiveLookupInfo info;
      return open_lookup(nullptr, LookupRegisteredArchiveFile(nullptr, filename, &info), info);
    }
#endif
    if (!file->OpenLoose(ToNativePath(resolved_path).string().c_str(), "rb")) {
      openwow::core::SErrSetLastError(2);
      return 0;
    }
    return finalize(std::move(file), 1, true);
  }
  if (type != 0) return 0;
  auto file = std::make_shared<RuntimeFile>(0);
  file->logical_path = filename;
  if (!file->OpenLoose(ToNativePath(resolved_path).string().c_str(), "rb")) {
    openwow::core::SErrSetLastError(2);
    return 0;
  }
  return finalize(std::move(file), 2, true);
}

int SFile_SetFilePointer(int handle, std::int32_t offset_low, std::int32_t,
                         std::uint32_t move_method) {
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file || !file->handle.critical_section) return 0;
  StormCriticalSectionScope lock(file->critical_section());
  switch (file->handle.type) {
  case 0: file->SetCursor(file->ApplyNativeCursorMove(offset_low, move_method)); break;
  case 2:
  case 3: file->SetCursor(file->ApplyArchiveCursorMove(offset_low, move_method)); break;
  case 5: file->SetCursor(file->ApplyBufferedCursorMove(offset_low, move_method)); break;
  default: return 0;
  }
  return static_cast<std::int32_t>(file->position);
}

int SFile_GetFileSize(int handle, std::uint32_t *out_high) {
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file) return 0;
  if (file->handle.type == 1 || file->handle.type == 5) return file->handle.file_size;
  if (file->handle.type != 0 && file->handle.type != 2 && file->handle.type != 3) return 0;
  std::uint64_t size = 0;
  if (!file->RefreshSize(&size)) return file->handle.type == 0 ? 0 : -1;
  if (out_high) *out_high = static_cast<std::uint32_t>(size >> 32);
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(size));
}

std::uint8_t *SFileReadFatalFlag_Get() { return RuntimeFile::FatalReadFlag(); }
std::uint8_t *SFileReadFatalFlag_Set(std::uint8_t value) { return RuntimeFile::SetFatalReadFlag(value); }

int SFile_ReadFile(int handle, void *buffer, int size, std::uint32_t *out_bytes_read,
                   int decompress, int) {
  openwow::data::ResetCurrentStreamingStatusChain();
  if (auto &hook = MutableSFileReadPrologueHookForTests(); hook) hook();
  if (decompress) return 0;
  if (size == 0) {
    if (out_bytes_read) *out_bytes_read = 0;
    return 1;
  }
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file) return 0;
  const std::uint32_t requested = static_cast<std::uint32_t>(size);
  if (file->handle.type == 0) {
    std::uint32_t actual = requested;
    if (!IOUnitContainer_ReadFileHandle(handle, buffer, &actual)) {
      openwow::core::StormSetLastError(38);
      return 0;
    }
    if (out_bytes_read) *out_bytes_read = actual;
    if (actual == requested) return 1;
    openwow::core::StormSetLastError(38);
    return 0;
  }
  if (file->handle.type >= 1 && file->handle.type <= 4) {
    if (!file->handle.critical_section) {
      openwow::core::StormSetLastError(38);
      return 0;
    }
    std::uint32_t actual = 0;
    const bool ok = ReadArchiveHandle(*file, buffer, requested, &actual);
    if (ok) {
      if (out_bytes_read) *out_bytes_read = actual;
      if (actual == requested) return 1;
    } else if (ShouldRaiseSFileReadFatal(*file)) {
      RaiseSFileReadFatal(*file);
    }
    openwow::core::StormSetLastError(38);
    return 0;
  }
  if (file->handle.type == 5) {
    if (!file->handle.critical_section) {
      openwow::core::StormSetLastError(38);
      return 0;
    }
    std::uint32_t actual = requested;
    if (!file->ReadCurrent(buffer, &actual, false)) {
      openwow::core::StormSetLastError(38);
      return 0;
    }
    if (out_bytes_read) *out_bytes_read = actual;
    if (actual == requested) return 1;
    openwow::core::StormSetLastError(38);
  }
  return 0;
}

int SFileOpenFileAndLoadData(void *archive, const char *filename, void **out_data,
                              std::size_t *out_size, std::size_t padding, int open_flags,
                              int read_flags) {
  if (!out_data) return 0;
  int handle = 0;
  if (!SFileOpenFile(archive, filename, open_flags, &handle)) return 0;
  const auto size = static_cast<std::uint32_t>(SFile_GetFileSize(handle, nullptr));
  if (padding > std::numeric_limits<std::size_t>::max() - size) {
    (void)IOUnitContainer_CloseFileHandle(handle);
    return 0;
  }
  const std::size_t allocation_size = static_cast<std::size_t>(size) + padding;
  std::unique_ptr<std::byte, decltype(&std::free)> buffer(
      static_cast<std::byte *>(std::malloc(allocation_size == 0 ? 1 : allocation_size)),
      &std::free);
  if (!buffer) {
    (void)IOUnitContainer_CloseFileHandle(handle);
    return 0;
  }
  if (read_flags != 0) {
    if (const auto file = RetailRuntimeFileRegistry().LookupRetained(handle)) file->handle.field_24 = 1;
  }
  std::uint32_t bytes_read = 0;
  if (!SFile_ReadFile(handle, buffer.get(), static_cast<int>(size), &bytes_read, read_flags, 0) ||
      bytes_read != size) {
    (void)IOUnitContainer_CloseFileHandle(handle);
    return 0;
  }
  if (padding) std::memset(buffer.get() + size, 0, padding);
  *out_data = buffer.release();
  if (out_size) *out_size = size;
  if (read_flags == 0) (void)IOUnitContainer_CloseFileHandle(handle);
  return 1;
}

bool SFileReadFileToBuffer(void *archive, const char *filename, void **out_data, int *out_size,
                           std::size_t padding, int open_flags) {
  *out_data = nullptr;
  if (out_size) *out_size = 0;
  std::size_t loaded_size = 0;
  if (!SFileOpenFileAndLoadData(archive, filename, out_data, &loaded_size, padding, open_flags, 0))
    return false;
  if (out_size) *out_size = static_cast<int>(loaded_size);
  return true;
}

bool SFileReadFileToBuffer_SetLastError(void *archive, const char *filename, void **out_data,
                                        int *out_size, std::size_t padding, int open_flags, int) {
  if (!SFileReadFileToBuffer(archive, filename, out_data, out_size, padding, open_flags)) {
    openwow::core::SErrSetLastError(openwow::core::GetStormLastError());
    return false;
  }
  return true;
}

int SFileReadFileToBuffer_Wrapper(const char *filename, void **out_data, std::size_t *out_size,
                                  std::size_t padding, int read_flags) {
  return SFileOpenFileAndLoadData(nullptr, filename, out_data, out_size, padding, 0, read_flags);
}
int SFileOpenFile_Wrapper(const char *filename, int *out_file) {
  return SFileOpenFile(nullptr, filename, 0, out_file);
}
int SFileFreeLoadedData(void *block) {
  std::free(block);
  return 1;
}
bool SFileCanResolvePath(const char *filename, int flags) {
  char path[260]{};
  std::int32_t type = 0;
  return SFileOpenFile_ResolveLoosePath(filename, path, sizeof(path),
                                        static_cast<std::uint8_t>(flags), &type) != 0;
}
int SFileHandle_CopyLogicalPathBounded(int handle, char *output, int capacity) {
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  openwow::core::CopyStormPath(output, file && !file->logical_path.empty()
                                           ? file->logical_path.c_str() : "", capacity);
  return file ? 1 : 0;
}
int AsyncFileRead_RequestDataPreloadPathAvailability(int handle, int queue_index,
                                                     char wait_for_completion) {
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file || file->logical_path.empty()) return 0;
  return RequestDataPreloadPathAvailability(file->logical_path.c_str(), queue_index,
                                             wait_for_completion != 0) ? 1 : 0;
}
bool OpenLooseFileHandle(const char *path, const char *mode, int *out_handle) {
  if (!path || !mode || !out_handle) return false;
  *out_handle = 0;
  auto file = std::make_shared<RuntimeFile>(0);
  file->logical_path = path;
  if (!file->OpenLoose(path, mode)) return false;
  *out_handle = RetailRuntimeFileRegistry().Store(std::move(file));
  return true;
}
bool QueryRuntimeSFileHandleMetadata(int handle, RuntimeSFileHandleMetadata *out_metadata) {
  if (!out_metadata) return false;
  *out_metadata = {};
  const auto file = RetailRuntimeFileRegistry().LookupRetained(handle);
  if (!file) return false;
  *out_metadata = file->Metadata();
  return true;
}

void SetSFileReadPrologueHookForTests(std::function<void()> hook) {
  MutableSFileReadPrologueHookForTests() = std::move(hook);
}
void ResetSFileReadPrologueHookForTests() { MutableSFileReadPrologueHookForTests() = {}; }

}

#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/vfs/retail/runtime_file_registry.h"
#include "openwow/vfs/retail/streaming/streaming_file_adapter.h"
#include "openwow/core/io_unit_container.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_file_io.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/streaming_init.h"
#include "openwow/platform/adapters/win32/win32_compat.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace openwow::vfs {
namespace {

constexpr std::size_t kFileStackShutdownProviderSlotOffset = 0x78u;

void *&ActiveFileStackCallbackTable() {
  static void *callback_table = GetDefaultFileStackCallbackTable();
  return callback_table;
}

constexpr std::uint64_t CombineLowHighDwords(std::uint32_t low, std::uint32_t high) {
  return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32u);
}

void SplitQwordToDwords(std::uint64_t value, std::uint32_t &low, std::uint32_t &high) {
  low = static_cast<std::uint32_t>(value);
  high = static_cast<std::uint32_t>(value >> 32u);
}

class StormCriticalSectionScope {
public:
  explicit StormCriticalSectionScope(openwow::platform::StormCriticalSection *section)
      : section_(section) { section_->Enter(); }
  ~StormCriticalSectionScope() { section_->Leave(); }
private:
  openwow::platform::StormCriticalSection *section_;
};
}

void *GetActiveFileStackCallbackTable() { return ActiveFileStackCallbackTable(); }
void SetActiveFileStackCallbackTable(void *table) { ActiveFileStackCallbackTable() = table; }
void SetActiveFileStackCallbackTableForTests(void *table) { ActiveFileStackCallbackTable() = table; }
void ResetActiveFileStackCallbackTableForTests() { ActiveFileStackCallbackTable() = GetDefaultFileStackCallbackTable(); }

void SetIOUnitContainerFileStackCallbackTableForTests(void *callback_table) {
  SetActiveFileStackCallbackTableForTests(callback_table);
}
void ResetIOUnitContainerFileStackCallbackTableForTests() {
  ResetActiveFileStackCallbackTableForTests();
}
void *GetIOUnitContainerFileStackCallbackTableForTests() {
  return GetActiveFileStackCallbackTable();
}
void WriteFileStackDispatchSlotForTests(void *callback_table, std::size_t slot_offset,
                                        FileStackDispatchFn dispatch) {
  WriteFileStackDispatchSlot(callback_table, slot_offset, dispatch);
}
std::uint32_t RegisterFileStackCompatPointerForTests(const void *pointer) {
  return RegisterFileStackCompatPointer(pointer);
}
void *ResolveFileStackCompatPointerForTests(std::uint32_t token) {
  return ResolveFileStackCompatPointer(token);
}

void IOUnitContainerFileStack_ShutdownProviderChain() {
  auto *current_table = GetActiveFileStackCallbackTable();
  if (!current_table) return;

  FileStackEventRecord event_record;
  event_record.Write(0x00u,
                     static_cast<std::uint32_t>(kFileStackShutdownProviderSlotOffset));
  if (auto callback = ReadFileStackDirectDispatchSlot(
          current_table, kFileStackShutdownProviderSlotOffset)) {
    (void)callback(current_table, event_record.data());
  }

  const auto *default_table = GetDefaultFileStackCallbackTable();
  while (current_table) {
    auto *next_table = const_cast<void *>(ReadFileStackNextProvider(current_table));
    if (current_table != default_table) std::free(current_table);
    current_table = next_table;
  }
  SetActiveFileStackCallbackTable(nullptr);
}

template <typename T>
void WriteFileStackPathMetadataWord(void *query_output, const std::size_t offset, const T value) {
  if (!query_output) {
    return;
  }

  std::memcpy(static_cast<std::byte *>(query_output) + offset, &value, sizeof(value));
}

std::mutex &MutableFileStackCompatPointerMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::uint32_t, void *> &MutableFileStackCompatPointers() {
  static std::unordered_map<std::uint32_t, void *> pointers;
  return pointers;
}

std::uint32_t &MutableNextFileStackCompatPointerToken() {
  static std::uint32_t next_token = 1;
  return next_token;
}

std::uint32_t RegisterFileStackCompatPointer(const void *pointer) {
  if (pointer == nullptr) {
    return 0;
  }

  std::lock_guard lock(MutableFileStackCompatPointerMutex());
  auto &next_token = MutableNextFileStackCompatPointerToken();
  auto &pointers = MutableFileStackCompatPointers();

  std::uint32_t token = 0;
  do {
    token = next_token++;
    if (next_token == 0) {
      next_token = 1;
    }
  } while (token == 0 || pointers.contains(token));

  pointers.emplace(token, const_cast<void *>(pointer));
  return token;
}

void UnregisterFileStackCompatPointer(const std::uint32_t token) {
  if (token == 0) {
    return;
  }

  std::lock_guard lock(MutableFileStackCompatPointerMutex());
  MutableFileStackCompatPointers().erase(token);
}

void *ResolveFileStackCompatPointer(const std::uint32_t token) {
  if (token == 0) {
    return nullptr;
  }

  std::lock_guard lock(MutableFileStackCompatPointerMutex());
  const auto it = MutableFileStackCompatPointers().find(token);
  return it != MutableFileStackCompatPointers().end() ? it->second : nullptr;
}

template <typename T>
T ReadFileStackPathQueryField(const void *event_record, const std::size_t offset) {
  T value{};
  if (!event_record) {
    return value;
  }

  if constexpr (std::is_pointer_v<T>) {
    std::uint32_t token = 0;
    std::memcpy(&token, static_cast<const std::byte *>(event_record) + offset, sizeof(token));
    if (void *resolved = ResolveFileStackCompatPointer(token); resolved != nullptr) {
      return static_cast<T>(resolved);
    }
  }

  std::memcpy(&value, static_cast<const std::byte *>(event_record) + offset, sizeof(value));
  return value;
}

constexpr std::size_t kFileStackDispatchRegistryCapacity = 256u;
constexpr std::size_t kFileStackNextProviderOffset = 0x04u;
constexpr std::size_t kFileStackSetWorkingDirectorySlotOffset = 0x08u;
constexpr std::size_t kFileStackCloseSlotOffset = 0x0Cu;
constexpr std::size_t kFileStackStatus9FailureSlotOffset = 0x10u;
constexpr std::size_t kFileStackGetWorkingDirectorySlotOffset = 0x14u;
constexpr std::size_t kFileStackEnumerateDirectoryEntriesSlotOffset = 0x18u;
constexpr std::size_t kFileStackQueryPathAttributesSlotOffset = 0x1Cu;
constexpr std::size_t kFileStackQueryPathMetadataSlotOffset = 0x24u;
constexpr std::size_t kFileStackGetFileHandlePointerSlotOffset = 0x2Cu;
constexpr std::size_t kFileStackCommitFileHandleMetadataSlotOffset = 0x3Cu;
constexpr std::size_t kFileStackCreateDirectorySlotOffset = 0x40u;
constexpr std::size_t kFileStackMovePathSlotOffset = 0x44u;
constexpr std::size_t kFileStackCopyPathSlotOffset = 0x48u;
constexpr std::size_t kFileStackOpenSlotOffset = 0x4Cu;
constexpr std::size_t kFileStackRemoveDirectorySlotOffset = 0x58u;
constexpr std::size_t kFileStackReopenFileHandleSlotOffset = 0x5Cu;
constexpr std::size_t kFileStackSetPathAttributesSlotOffset = 0x64u;
constexpr std::size_t kFileStackSetFileHandlePointerSlotOffset = 0x68u;
constexpr std::size_t kFileStackDeleteFileSlotOffset = 0x6Cu;

constexpr std::uint32_t kFileStackDirtyFileHandleAttributes = 0x04u;
constexpr std::uint32_t kFileStackDirtyFileHandleLastWriteTime = 0x10u;
constexpr std::uint16_t kWin32FileAttributeArchive = 0x20u;

std::array<FileStackDispatchFn, kFileStackDispatchRegistryCapacity> g_file_stack_dispatch_registry{};
std::atomic<std::uint32_t> g_file_stack_dispatch_registry_count{1u};
std::mutex g_file_stack_dispatch_registry_mutex;

std::uint32_t RegisterFileStackDispatch(FileStackDispatchFn dispatch) {
  if (!dispatch) {
    return 0;
  }

  const auto registered_count =
      g_file_stack_dispatch_registry_count.load(std::memory_order_acquire);
  for (std::uint32_t index = 1; index < registered_count; ++index) {
    if (g_file_stack_dispatch_registry[index] == dispatch) {
      return index;
    }
  }

  std::lock_guard lock(g_file_stack_dispatch_registry_mutex);
  const auto locked_count =
      g_file_stack_dispatch_registry_count.load(std::memory_order_relaxed);
  for (std::uint32_t index = 1; index < locked_count; ++index) {
    if (g_file_stack_dispatch_registry[index] == dispatch) {
      return index;
    }
  }

  if (locked_count >= kFileStackDispatchRegistryCapacity) {
    throw std::runtime_error("File-stack dispatch registry capacity exceeded");
  }

  g_file_stack_dispatch_registry[locked_count] = dispatch;
  g_file_stack_dispatch_registry_count.store(locked_count + 1u, std::memory_order_release);
  return locked_count;
}

FileStackDispatchFn ResolveFileStackDispatch(const std::uint32_t token) {
  if (token == 0) {
    return nullptr;
  }

  const auto registered_count =
      g_file_stack_dispatch_registry_count.load(std::memory_order_acquire);
  if (token >= registered_count) {
    return nullptr;
  }

  return g_file_stack_dispatch_registry[token];
}

void WriteFileStackDispatchSlot(void *callback_table, const std::size_t slot_offset,
                                FileStackDispatchFn dispatch) {
  if (!callback_table) {
    return;
  }

  const auto token = RegisterFileStackDispatch(dispatch);
  std::memcpy(static_cast<std::byte *>(callback_table) + slot_offset, &token, sizeof(token));
}

const void *ReadFileStackNextProvider(const void *callback_table) {
  if (!callback_table) {
    return nullptr;
  }

  return ReadFileStackPathQueryField<const void *>(callback_table, kFileStackNextProviderOffset);
}

FileStackDispatchFn ReadFileStackDispatchSlot(const void *callback_table,
                                              const std::size_t slot_offset) {
  for (auto current_table = callback_table; current_table;
       current_table = ReadFileStackNextProvider(current_table)) {
    std::uint32_t token = 0;
    std::memcpy(&token, static_cast<const std::byte *>(current_table) + slot_offset,
                sizeof(token));
    if (auto callback = ResolveFileStackDispatch(token)) {
      return callback;
    }
  }

  return nullptr;
}

FileStackDispatchFn ReadFileStackDirectDispatchSlot(const void *callback_table,
                                                    const std::size_t slot_offset) {
  if (!callback_table) {
    return nullptr;
  }

  std::uint32_t token = 0;
  std::memcpy(&token, static_cast<const std::byte *>(callback_table) + slot_offset,
              sizeof(token));
  return ResolveFileStackDispatch(token);
}

bool DispatchLocalFileStackSlot(void *callback_table, const std::size_t slot_offset,
                                 FileStackEventRecord &event_record) {
  if (!callback_table) {
    return false;
  }

  auto callback = ReadFileStackDispatchSlot(callback_table, slot_offset);
  return callback && callback(callback_table, event_record.data());
}

bool DispatchFileStackEvent(void *callback_table, std::size_t slot_offset,
                            FileStackEventRecord &event_record) {
  return DispatchLocalFileStackSlot(callback_table, slot_offset, event_record);
}

bool OpenRuntimeFileFromFileStackOpen(const char *logical_path, const std::uint32_t open_flags,
                                              int *out_handle);

enum class FileStackWin32CreationDisposition : std::uint32_t {
  kCreateNew = 1u,
  kCreateAlways = 2u,
  kOpenExisting = 3u,
  kOpenAlways = 4u,
};

struct FileStackWin32OpenParameters {
  std::uint32_t share_mode = 0;
  std::uint32_t desired_access = 0;
  FileStackWin32CreationDisposition creation_disposition =
      FileStackWin32CreationDisposition::kOpenAlways;
  std::uint32_t flags_and_attributes = 0;
};

FileStackWin32OpenParameters TranslateFileStackWin32OpenParameters(
    const std::uint32_t open_flags) {
  FileStackWin32OpenParameters parameters{};
  parameters.share_mode = (open_flags >> 2u) & 3u;
  parameters.desired_access = ((4u * open_flags) | (open_flags & 2u)) << 29u;

  if ((open_flags & 0x100u) == 0u && (open_flags & 0x400u) == 0u) {
    parameters.creation_disposition =
        (open_flags & 0x800u) != 0u
            ? FileStackWin32CreationDisposition::kCreateNew
            : (open_flags & 0x1000u) != 0u ? FileStackWin32CreationDisposition::kOpenExisting
                                           : FileStackWin32CreationDisposition::kOpenAlways;
  } else {
    parameters.creation_disposition =
        (open_flags & 0x800u) != 0u ? FileStackWin32CreationDisposition::kCreateNew
                                    : FileStackWin32CreationDisposition::kCreateAlways;
  }

  parameters.flags_and_attributes =
      2u * ((open_flags & 0x80u) | 0x40u)
      | ((open_flags & 0x40u) != 0u ? 0xA0000000u : 0u);
  return parameters;
}

bool FileStackWin32OpenWantsReadAccess(const FileStackWin32OpenParameters &parameters) {
  return (parameters.desired_access & 0x80000000u) != 0u;
}

bool FileStackWin32OpenWantsWriteAccess(const FileStackWin32OpenParameters &parameters) {
  return (parameters.desired_access & 0x40000000u) != 0u;
}

std::array<char, 1024> NormalizeFileStackWin32OpenPath(const char *path) {
  std::array<char, 1024> normalized{};
  openwow::core::NormalizePathToBackslashes(path, normalized.data(),
                                            static_cast<int>(normalized.size()));
  return normalized;
}

std::optional<std::filesystem::path> ResolveLooseFileOpenPath(
    const char *path, const FileStackWin32OpenParameters &parameters) {
  if (path == nullptr || path[0] == '\0') {
    return std::nullopt;
  }

  const auto existing_path =
      ResolveExistingPathCaseInsensitive(path, ExistingPathRequirement::kFileOnly);
  switch (parameters.creation_disposition) {
  case FileStackWin32CreationDisposition::kOpenExisting:
    return existing_path;
  case FileStackWin32CreationDisposition::kOpenAlways:
    if (existing_path.has_value()) {
      return existing_path;
    }
    return ResolvePathCaseInsensitiveForCreate(path);
  case FileStackWin32CreationDisposition::kCreateAlways:
    if (existing_path.has_value()) {
      return existing_path;
    }
    return ResolvePathCaseInsensitiveForCreate(path);
  case FileStackWin32CreationDisposition::kCreateNew:
    if (existing_path.has_value()) {
      return std::nullopt;
    }
    return ResolvePathCaseInsensitiveForCreate(path);
  }

  return std::nullopt;
}

bool CreateOrTruncateLooseFileForReadOnlyOpen(const std::filesystem::path &native_path) {
  std::ofstream stream(native_path, std::ios::binary | std::ios::trunc);
  return stream.is_open();
}

const char *SelectLooseFileOpenMode(const FileStackWin32OpenParameters &parameters,
                                    const bool path_exists) {
  const bool read_access = FileStackWin32OpenWantsReadAccess(parameters);
  const bool write_access = FileStackWin32OpenWantsWriteAccess(parameters);

  if (parameters.creation_disposition == FileStackWin32CreationDisposition::kCreateAlways ||
      parameters.creation_disposition == FileStackWin32CreationDisposition::kCreateNew) {
    if (write_access) {
      return read_access ? "w+b" : "wb";
    }
    return nullptr;
  }

  if (parameters.creation_disposition == FileStackWin32CreationDisposition::kOpenAlways &&
      !path_exists) {
    if (write_access) {
      return read_access ? "w+b" : "wb";
    }
    return nullptr;
  }

  if (read_access && write_access) {
    return "r+b";
  }
  if (write_access) {
    return "r+b";
  }
  if (read_access) {
    return "rb";
  }
  return nullptr;
}

bool TryOpenRuntimeFileFromFileStackOpen(RuntimeFile &runtime_handle,
                                                const char *logical_path,
                                                const std::uint32_t open_flags,
                                                const bool honor_append_seek) {
  const auto parameters = TranslateFileStackWin32OpenParameters(open_flags);
  const auto native_path = ResolveLooseFileOpenPath(logical_path, parameters);
  if (!native_path.has_value()) {
    return false;
  }

  const bool path_exists = std::filesystem::is_regular_file(*native_path);
  const char *mode = SelectLooseFileOpenMode(parameters, path_exists);
  runtime_handle.handle.field_04 = static_cast<std::int32_t>(open_flags);
  runtime_handle.logical_path = logical_path ? logical_path : "";

  const bool read_only_creation =
      mode == nullptr && !FileStackWin32OpenWantsWriteAccess(parameters)
      && FileStackWin32OpenWantsReadAccess(parameters);
  if (read_only_creation) {
    if (!CreateOrTruncateLooseFileForReadOnlyOpen(*native_path) ||
        !runtime_handle.OpenLoose(native_path->string().c_str(), "rb")) {
      return false;
    }
  } else if (mode == nullptr ||
             !runtime_handle.OpenLoose(native_path->string().c_str(), mode)) {
    return false;
  }

  if (honor_append_seek && (open_flags & 0x200u) != 0u) {
    runtime_handle.SetCursor(static_cast<std::int64_t>(runtime_handle.size));
  }

  return true;
}

bool OpenRuntimeFileFromFileStackOpen(const char *logical_path, const std::uint32_t open_flags,
                                             int *out_handle) {
  if (out_handle == nullptr) {
    return false;
  }

  *out_handle = 0;

  auto runtime_handle = std::make_shared<RuntimeFile>();
  runtime_handle->handle.type = 0;
  if (!TryOpenRuntimeFileFromFileStackOpen(*runtime_handle, logical_path, open_flags, true)) {
    openwow::core::StormSetLastError(4);
    return false;
  }

  *out_handle = RetailRuntimeFileRegistry().Store(std::move(runtime_handle));
  return true;
}

bool ReopenRuntimeFileWithFileStackFlags(RuntimeFile &runtime_handle,
                                                const char *reopen_path,
                                                const std::uint32_t requested_flags) {
  const std::uint32_t merged_flags =
      requested_flags | (static_cast<std::uint32_t>(runtime_handle.handle.field_04) & 0xFFFFFFBFu);
  runtime_handle.file_io.reset();
  if (!TryOpenRuntimeFileFromFileStackOpen(runtime_handle, reopen_path, merged_flags,
                                                  false)) {
    runtime_handle.file_io.reset();
    return false;
  }
  return true;
}

bool EnumerateMergedFileStackDirectoryEntries(
    const char *path, const std::function<bool(const FileSystemDirectoryEntry &)> &callback) {
  if (!path || !callback) {
    return false;
  }

  std::map<std::string, bool> entries;
  if (!FileSystem_EnumerateDirectoryEntries(path, [&entries](const FileSystemDirectoryEntry &entry) {
        entries.try_emplace(entry.entry_name ? entry.entry_name : "", entry.is_directory);
        return true;
      })) {
    return false;
  }

  for (const auto &entry_name : openwow::data::EnumerateFileManifestDirectoryFiles(path)) {
    entries.try_emplace(entry_name, false);
  }

  for (const auto &[entry_name, is_directory] : entries) {
    const FileSystemDirectoryEntry entry{
        .directory_path = path,
        .entry_name = entry_name.c_str(),
        .is_directory = is_directory,
    };
    if (!callback(entry)) {
      return false;
    }
  }

  return true;
}

void PopulateLegacyManifestPathMetadata(void *query_output, const char *path,
                                        const std::int64_t file_size) {
  if (!query_output) {
    return;
  }

  const auto raw_size = static_cast<std::uint64_t>(file_size);
  FileStackPathMetadata metadata{};
  metadata.field_00 =
      static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(path) & 0xFFFFFFFFu);
  SplitQwordToDwords(raw_size, metadata.file_size_low, metadata.file_size_high);
  metadata.translated_attributes =
      FileStack_TranslateWin32Attributes(kWin32FileAttributeArchive);
  metadata.object_kind = ClassifyLegacyManifestFileKind(file_size);
  metadata.provider_flags = 1u;
  std::memcpy(query_output, &metadata, sizeof(metadata));
}

void PopulateLooseFilePathMetadata(void *query_output,
                                   const LooseResolvedPathMetadata &metadata) {
  if (!query_output) {
    return;
  }

  FileStackPathMetadata path_metadata{};
  SplitQwordToDwords(metadata.size, path_metadata.file_size_low, path_metadata.file_size_high);
  path_metadata.translated_attributes = metadata.translated_attributes;
  path_metadata.creation_time_ns_since_2000 = metadata.creation_time_ns_since_2000;
  path_metadata.last_access_time_ns_since_2000 = metadata.last_access_time_ns_since_2000;
  path_metadata.last_write_time_ns_since_2000 = metadata.last_write_time_ns_since_2000;
  path_metadata.object_kind = metadata.object_kind;
  path_metadata.provider_flags = metadata.non_directory_hint ? 1u : 0u;
  std::memcpy(query_output, &path_metadata, sizeof(path_metadata));
}

bool DefaultIOUnitContainer_QueryPathAttributesDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  void *const query_output = ReadFileStackPathQueryField<void *>(event_record, 0x10u);

  if (!path) {
    const std::uint32_t file_kind = 0;
    WriteFileStackPathMetadataWord(query_output, 0x28u, file_kind);
    return true;
  }

  const auto file_size = LookupLegacyManifestFileSize(path);
  if (file_size.has_value()) {
    const auto file_kind = ClassifyLegacyManifestFileKind(*file_size);
    WriteFileStackPathMetadataWord(query_output, 0x28u, file_kind);
    return true;
  }

  const auto metadata = QueryLooseResolvedPathMetadata(path);
  if (!metadata.has_value()) {
    return false;
  }

  WriteFileStackPathMetadataWord(query_output, 0x28u, metadata->object_kind);
  return true;
}

bool DefaultIOUnitContainer_QueryPathMetadataDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  void *const query_output = ReadFileStackPathQueryField<void *>(event_record, 0x10u);

  if (!path) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  const auto file_size = LookupLegacyManifestFileSize(path);
  if (file_size.has_value()) {
    PopulateLegacyManifestPathMetadata(query_output, path, *file_size);
    return true;
  }

  const auto metadata = QueryLooseResolvedPathMetadata(path);
  if (!metadata.has_value()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  PopulateLooseFilePathMetadata(query_output, *metadata);
  return true;
}

bool DefaultIOUnitContainer_EnumerateDirectoryEntriesDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  auto *bridge =
      ReadFileStackPathQueryField<FileStackDirectoryEnumerationBridge *>(event_record, 0x80u);
  if (!path || !bridge || !bridge->callback || !*bridge->callback) {
    return false;
  }

  return EnumerateMergedFileStackDirectoryEntries(path, *bridge->callback);
}

bool DefaultIOUnitContainer_GetFileHandlePointerDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  std::uint32_t handle_word = 0;
  std::memcpy(&handle_word, static_cast<const std::byte *>(event_record) + 0x0C,
              sizeof(handle_word));

  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(static_cast<int>(handle_word));
  if (!runtime_handle) {
    return false;
  }

  const auto cursor = static_cast<std::uint64_t>(runtime_handle->position);
  const auto cursor_low = static_cast<std::uint32_t>(cursor & 0xFFFFFFFFu);
  const auto cursor_high = static_cast<std::uint32_t>(cursor >> 32);
  std::memcpy(static_cast<std::byte *>(event_record) + 0x68u, &cursor_low, sizeof(cursor_low));
  std::memcpy(static_cast<std::byte *>(event_record) + 0x6Cu, &cursor_high,
              sizeof(cursor_high));
  return true;
}

bool DefaultIOUnitContainer_SetFileHandlePointerDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *event_bytes = static_cast<const std::byte *>(event_record);
  std::uint32_t handle_word = 0;
  std::uint32_t offset_low = 0;
  std::uint32_t offset_high = 0;
  std::uint32_t move_method = 0;
  std::memcpy(&handle_word, event_bytes + 0x0Cu, sizeof(handle_word));
  std::memcpy(&offset_low, event_bytes + 0x68u, sizeof(offset_low));
  std::memcpy(&offset_high, event_bytes + 0x6Cu, sizeof(offset_high));
  std::memcpy(&move_method, event_bytes + 0x70u, sizeof(move_method));

  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(static_cast<int>(handle_word));
  if (!runtime_handle || !runtime_handle->handle.critical_section) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  const std::uint32_t normalized_move_method =
      move_method == 0u ? 0u : move_method == 1u ? 1u : 2u;
  const auto signed_offset =
      static_cast<std::int64_t>(CombineLowHighDwords(offset_low, offset_high));

  StormCriticalSectionScope handle_lock(runtime_handle->critical_section());
  const auto current_position = runtime_handle->position;
  std::int64_t target_position = current_position;
  switch (normalized_move_method) {
  case 0u:
    target_position = signed_offset;
    break;
  case 1u:
    target_position = current_position + signed_offset;
    break;
  case 2u:
    target_position = static_cast<std::int64_t>(runtime_handle->size) + signed_offset;
    break;
  default:
    return false;
  }

  if (target_position < 0) {
    return false;
  }

  runtime_handle->SetCursor(target_position);
  return true;
}

bool DefaultIOUnitContainer_CommitFileHandleMetadataDispatch(void * ,
                                                             void *event_record) {
  if (!event_record) {
    return false;
  }

  auto *event_bytes = static_cast<std::byte *>(event_record);
  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const auto *metadata =
      ReadFileStackPathQueryField<const FileStackPathMetadata *>(event_record, 0x10u);
  std::uint32_t handle_word = 0;
  std::uint32_t dirty_flags = 0;
  std::memcpy(&handle_word, event_bytes + 0x0Cu, sizeof(handle_word));
  std::memcpy(&dirty_flags, event_bytes + 0x50u, sizeof(dirty_flags));

  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(static_cast<int>(handle_word));

  if ((dirty_flags & kFileStackDirtyFileHandleLastWriteTime) != 0u) {
    if (!runtime_handle || !runtime_handle->handle.critical_section || !runtime_handle->file_io
        || !metadata) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      std::memcpy(event_bytes + 0x50u, &dirty_flags, sizeof(dirty_flags));
      return false;
    }

    const auto last_write_time_ns_since_2000 =
        ReadFileStackPathQueryField<std::int64_t>(metadata, 0x20u);
    if (!runtime_handle->SetLastWriteTime(last_write_time_ns_since_2000)) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      std::memcpy(event_bytes + 0x50u, &dirty_flags, sizeof(dirty_flags));
      return false;
    }
    dirty_flags &= ~kFileStackDirtyFileHandleLastWriteTime;
  }

  if ((dirty_flags & kFileStackDirtyFileHandleAttributes) != 0u) {
    if (!metadata) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      std::memcpy(event_bytes + 0x50u, &dirty_flags, sizeof(dirty_flags));
      return false;
    }

    const auto translated_attributes =
        ReadFileStackPathQueryField<std::uint32_t>(metadata, 0x0Cu);
    const auto native_attributes = FileStack_TranslateMetadataAttributesToWin32(
        static_cast<std::uint8_t>(translated_attributes));
    if (!FileSystem_SetPathAttributes(path, native_attributes)) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      std::memcpy(event_bytes + 0x50u, &dirty_flags, sizeof(dirty_flags));
      return false;
    }

    if (runtime_handle) {
      runtime_handle->UpdateTranslatedAttributes(translated_attributes);
    }
    dirty_flags &= ~kFileStackDirtyFileHandleAttributes;
  }

  std::memcpy(event_bytes + 0x50u, &dirty_flags, sizeof(dirty_flags));
  return dirty_flags == 0u;
}

bool DefaultIOUnitContainer_OpenDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const auto open_flags = ReadFileStackPathQueryField<std::uint32_t>(event_record, 0x58u);
  int handle = 0;
  if (!TryOpenStreamingPartBackingHandle(ActiveFileStackCallbackTable(), path,
                                         open_flags, &handle)) {
    const auto normalized_path = NormalizeFileStackWin32OpenPath(path);
    if (!OpenRuntimeFileFromFileStackOpen(normalized_path.data(), open_flags, &handle)) {
      return false;
    }
  }

  if (handle == 0) {
    return false;
  }

  std::memcpy(static_cast<std::byte *>(event_record) + 0x0Cu, &handle, sizeof(handle));
  return true;
}

bool DefaultIOUnitContainer_SetWorkingDirectoryDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  if (!path) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  return SetNativeWorkingDirectory(path);
}

bool DefaultIOUnitContainer_CloseDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  std::uint32_t handle_word = 0;
  std::memcpy(&handle_word, static_cast<const std::byte *>(event_record) + 0x0C,
              sizeof(handle_word));
  return RetailRuntimeFileRegistry().Remove(static_cast<int>(handle_word));
}

bool DefaultIOUnitContainer_FailWithStatusCode9Dispatch(void * ,
                                                        void * ) {
  openwow::data::SetCurrentStreamingStatusCode(9);
  return false;
}

bool DefaultIOUnitContainer_GetWorkingDirectoryDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  auto *path = ReadFileStackPathQueryField<char *>(event_record, 0x74u);
  const auto path_capacity = ReadFileStackPathQueryField<int>(event_record, 0x78u);
  if (!path || path_capacity <= 0) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  path[0] = '\0';
  return QueryNativeWorkingDirectory(path, path_capacity);
}

bool DefaultIOUnitContainer_CreateDirectoryDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const bool recursive =
      ReadFileStackPathQueryField<std::uint8_t>(event_record, 0x6Cu) != 0u;
  return FileSystem_CreateDirectory(path, recursive);
}

bool MoveLoosePath(const char *source_path, const char *destination_path) {
  if (!source_path || !destination_path || *source_path == '\0' || *destination_path == '\0') {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  std::array<char, 1024> normalized_source{};
  std::array<char, 1024> normalized_destination{};
  openwow::core::NormalizePathToBackslashes(
      source_path, normalized_source.data(), static_cast<int>(normalized_source.size()));
  openwow::core::NormalizePathToBackslashes(
      destination_path, normalized_destination.data(),
      static_cast<int>(normalized_destination.size()));

  const auto source_info = ResolveExistingPathInfoCaseInsensitive(normalized_source.data());
  if (!source_info.has_value()
      || ResolveExistingPathInfoCaseInsensitive(normalized_destination.data()).has_value()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  const auto resolved_destination =
      ResolvePathCaseInsensitiveForCreate(normalized_destination.data());
  if (!resolved_destination.has_value()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  std::error_code ec;
  std::filesystem::rename(source_info->actual_path, *resolved_destination, ec);
  if (!ec) {
    return true;
  }

  if (ec == std::make_error_code(std::errc::cross_device_link)
      && source_info->is_regular_file
      && CopyLooseFilePath(normalized_source.data(), normalized_destination.data(), false)) {
    ec.clear();
    if (std::filesystem::remove(source_info->actual_path, ec) && !ec) {
      return true;
    }
  }

  openwow::data::SetCurrentStreamingStatusCode(8);
  return false;
}

bool DefaultIOUnitContainer_MovePathDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *source_path =
      ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const auto *destination_path =
      ReadFileStackPathQueryField<const char *>(event_record, 0x08u);
  return MoveLoosePath(source_path, destination_path);
}

bool DefaultIOUnitContainer_CopyPathDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *source_path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const auto *destination_path =
      ReadFileStackPathQueryField<const char *>(event_record, 0x08u);
  const auto overwrite_existing =
      ReadFileStackPathQueryField<std::uint8_t>(event_record, 0x89u);
  return CopyLooseFilePath(source_path, destination_path, overwrite_existing != 0);
}

bool RemoveLooseDirectoryPath(const char *path) {
  if (!path || *path == '\0') {
    return false;
  }

  char resolved_directory[260] = {};
  if (!ResolveExistingPathAbsolute(path, resolved_directory,
                                   static_cast<int>(sizeof(resolved_directory)),
                                   ExistingPathRequirement::kDirectoryOnly)) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::remove(ToNativePath(resolved_directory), ec) && !ec;
}

bool DefaultIOUnitContainer_RemoveDirectoryDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  if (ReadFileStackPathQueryField<std::uint8_t>(event_record, 0x7Cu) != 0) {
    return FileSystem_RemoveDirectoryTree(path, false);
  }

  if (!path) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  return RemoveLooseDirectoryPath(path);
}

bool DefaultIOUnitContainer_ReopenFileHandleDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  std::uint32_t handle_word = 0;
  std::memcpy(&handle_word, static_cast<const std::byte *>(event_record) + 0x0Cu,
              sizeof(handle_word));
  const auto requested_flags = ReadFileStackPathQueryField<std::uint32_t>(event_record, 0x58u);
  const auto runtime_handle = RetailRuntimeFileRegistry().LookupRetained(static_cast<int>(handle_word));
  if (!runtime_handle || !runtime_handle->handle.critical_section || !runtime_handle->file_io) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  const std::string reopen_path = runtime_handle->logical_path.empty() ? runtime_handle->native_path
                                                                       : runtime_handle->logical_path;
  if (reopen_path.empty()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  StormCriticalSectionScope handle_lock(runtime_handle->critical_section());
  if (!ReopenRuntimeFileWithFileStackFlags(*runtime_handle, reopen_path.c_str(),
                                                  requested_flags)) {
    openwow::data::SetCurrentStreamingStatusCode(4);
    return false;
  }

  return true;
}

bool DefaultIOUnitContainer_DeleteFileDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  const auto resolved_path =
      ResolveExistingPathCaseInsensitive(path, ExistingPathRequirement::kFileOnly);
  if (!resolved_path.has_value()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::remove(*resolved_path, ec) && !ec;
}

bool DefaultIOUnitContainer_SetPathAttributesDispatch(void * , void *event_record) {
  if (!event_record) {
    return false;
  }

  const auto *event_bytes = static_cast<const std::byte *>(event_record);
  const auto *path = ReadFileStackPathQueryField<const char *>(event_record, 0x04u);
  std::uint32_t file_attributes = 0;
  std::memcpy(&file_attributes, event_bytes + 0x28u, sizeof(file_attributes));
  return FileSystem_SetPathAttributes(path, file_attributes);
}

struct DefaultIOUnitContainerFileStackCallbackTableStorage {
  std::array<std::byte, 0x7C> storage{};
};
static_assert(sizeof(DefaultIOUnitContainerFileStackCallbackTableStorage) == 0x7C,
              "Default file-stack callback table must preserve the legacy slot footprint");

void *GetDefaultFileStackCallbackTable() {
  static DefaultIOUnitContainerFileStackCallbackTableStorage callback_table{};
  static std::once_flag init_once;
  std::call_once(init_once, []() {
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackSetWorkingDirectorySlotOffset,
                               &DefaultIOUnitContainer_SetWorkingDirectoryDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackCloseSlotOffset,
                               &DefaultIOUnitContainer_CloseDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackStatus9FailureSlotOffset,
                               &DefaultIOUnitContainer_FailWithStatusCode9Dispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackGetWorkingDirectorySlotOffset,
                               &DefaultIOUnitContainer_GetWorkingDirectoryDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackEnumerateDirectoryEntriesSlotOffset,
                               &DefaultIOUnitContainer_EnumerateDirectoryEntriesDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackQueryPathAttributesSlotOffset,
                               &DefaultIOUnitContainer_QueryPathAttributesDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackQueryPathMetadataSlotOffset,
                               &DefaultIOUnitContainer_QueryPathMetadataDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackGetFileHandlePointerSlotOffset,
                               &DefaultIOUnitContainer_GetFileHandlePointerDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackCommitFileHandleMetadataSlotOffset,
                               &DefaultIOUnitContainer_CommitFileHandleMetadataDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackCreateDirectorySlotOffset,
                               &DefaultIOUnitContainer_CreateDirectoryDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackMovePathSlotOffset,
                               &DefaultIOUnitContainer_MovePathDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackOpenSlotOffset,
                               &DefaultIOUnitContainer_OpenDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackCopyPathSlotOffset,
                               &DefaultIOUnitContainer_CopyPathDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackRemoveDirectorySlotOffset,
                               &DefaultIOUnitContainer_RemoveDirectoryDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackReopenFileHandleSlotOffset,
                               &DefaultIOUnitContainer_ReopenFileHandleDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackSetFileHandlePointerSlotOffset,
                               &DefaultIOUnitContainer_SetFileHandlePointerDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(),
                               kFileStackSetPathAttributesSlotOffset,
                               &DefaultIOUnitContainer_SetPathAttributesDispatch);
    WriteFileStackDispatchSlot(callback_table.storage.data(), kFileStackDeleteFileSlotOffset,
                               &DefaultIOUnitContainer_DeleteFileDispatch);
  });
  return callback_table.storage.data();
}

}

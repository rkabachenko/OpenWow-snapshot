#include "openwow/vfs/retail/io_unit/io_unit_compat.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/retail_path_resolver.h"
#include "openwow/vfs/retail/file_stack/file_stack_abi.h"
#include "openwow/vfs/retail/file_stack/file_stack_provider.h"
#include "openwow/vfs/retail/runtime_file.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_file_io.h"
#include "openwow/core/storm_path.h"
#include "openwow/core/storm_string.h"

#include "openwow/core/storm_utils.h"
#include "openwow/core/streaming_storage.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/data/streaming_init.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/platform/adapters/win32/win32_compat.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openwow::vfs {
using openwow::core::AppendStormPath;
using openwow::core::CopyStormPath;

constexpr std::uint16_t kWin32FileAttributeReadOnly = 0x0001u;
constexpr std::uint16_t kWin32FileAttributeHidden = 0x0002u;
constexpr std::uint16_t kWin32FileAttributeSystem = 0x0004u;
constexpr std::uint16_t kWin32FileAttributeDirectory = 0x0010u;
constexpr std::uint16_t kWin32FileAttributeArchive = 0x0020u;
constexpr std::uint16_t kWin32FileAttributeNormal = 0x0080u;
constexpr std::uint16_t kWin32FileAttributeTemporary = 0x0100u;

constexpr std::uint16_t kWin32SettableFileAttributes = static_cast<std::uint16_t>(
    kWin32FileAttributeReadOnly | kWin32FileAttributeHidden | kWin32FileAttributeSystem |
    kWin32FileAttributeDirectory | kWin32FileAttributeArchive | kWin32FileAttributeNormal |
    kWin32FileAttributeTemporary);
constexpr std::uint32_t kFileStackAttributeReadOnly = 0x0001u;
constexpr std::uint32_t kFileStackAttributeHidden = 0x0002u;
constexpr std::uint32_t kFileStackAttributeNonDirectory = 0x0020u;
constexpr std::uint32_t kFileStackAttributeDirectory = 0x0040u;

std::function<bool(char *, int)> &MutableFileSystemGetWorkingDirectoryHookForTests() {
  static std::function<bool(char *, int)> hook;
  return hook;
}
std::function<bool(const char *)> &MutableFileSystemSetWorkingDirectoryHookForTests() {
  static std::function<bool(const char *)> hook;
  return hook;
}

void ResetLooseFileAttributeOverridesForTests() {
  LooseFileMetadataStore::ResetForTests();
}
std::filesystem::path ToNativePath(const char *raw_path) {
  std::string normalized = raw_path ? raw_path : "";
#if defined(_WIN32)
  std::replace(normalized.begin(), normalized.end(), '/', '\\');
  return std::filesystem::path(openwow::core::Utf8ToCurrentCodePageString(normalized.c_str()));
#else
  std::replace(normalized.begin(), normalized.end(), '\\',
               std::filesystem::path::preferred_separator);
  return std::filesystem::path(normalized);
#endif
}

std::string ToStormPathString(const std::filesystem::path &path) {
#if defined(_WIN32)
  const std::string native_path = path.string();
  std::string storm_path = openwow::core::CurrentCodePageToUtf8String(native_path.c_str());
#else
  std::string storm_path = path.string();
#endif
  std::replace(storm_path.begin(), storm_path.end(), '/', '\\');
  return storm_path;
}

std::string ToUtf8FilesystemPathString(const std::filesystem::path &path) {
#if defined(_WIN32)
  std::string utf8_path =
      openwow::core::CurrentCodePageToUtf8String(path.string().c_str());
#else
  std::string utf8_path = path.string();
#endif
  std::replace(utf8_path.begin(), utf8_path.end(), '\\', '/');
  return utf8_path;
}

std::filesystem::path BuildAbsoluteFilesystemPath(const char *source) {
#if defined(_WIN32)
  const std::string ansi_source = openwow::core::Utf8ToCurrentCodePageString(source);
  std::array<char, 1024> ansi_absolute_path{};
  if (_fullpath(ansi_absolute_path.data(), ansi_source.c_str(),
                static_cast<int>(ansi_absolute_path.size())) != nullptr) {
    return std::filesystem::path(ansi_absolute_path.data());
  }
#endif
  std::filesystem::path absolute_path = ToNativePath(source);
  if (!absolute_path.is_absolute()) {
    std::error_code ec;
    const std::filesystem::path current_directory = std::filesystem::current_path(ec);
    if (!ec) absolute_path = current_directory / absolute_path;
  }
  return absolute_path.lexically_normal();
}

std::optional<std::filesystem::path>
ResolveDirectoryEntryCaseInsensitive(const std::filesystem::path &directory,
                                     const std::string &component) {
  std::error_code ec;
  if (!std::filesystem::is_directory(directory, ec)) {
    return std::nullopt;
  }

  for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }

    const auto entry_name = entry.path().filename().string();
    if (openwow::text::EqualsIgnoreCaseAscii(entry_name.c_str(), component.c_str())) {
      return entry.path();
    }
  }

  return std::nullopt;
}

bool MatchesExistingPathRequirement(const ResolvedExistingPathInfo &info,
                                    const ExistingPathRequirement requirement) {
  switch (requirement) {
  case ExistingPathRequirement::kFileOnly:
    return info.is_regular_file;
  case ExistingPathRequirement::kDirectoryOnly:
    return info.is_directory;
  case ExistingPathRequirement::kFileOrDirectory:
    return true;
  }

  return false;
}

std::optional<ResolvedExistingPathInfo>
ResolveExistingPathInfoCaseInsensitive(const char *candidate) {
  if (!candidate || !*candidate) {
    return std::nullopt;
  }

  const auto native = ToNativePath(candidate);
  const bool absolute = native.is_absolute();

  std::error_code ec;
  const auto cwd = std::filesystem::current_path(ec);
  if (ec) {
    return std::nullopt;
  }

  std::filesystem::path current = absolute ? native.root_path() : cwd;
  const auto remainder = absolute ? native.relative_path() : native;
  for (const auto &component_path : remainder) {
    const auto component = component_path.string();
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == "..") {
      current = current.parent_path();
      continue;
    }

    const auto matched = ResolveDirectoryEntryCaseInsensitive(current, component);
    if (!matched.has_value()) {
      return std::nullopt;
    }
    current = *matched;
  }

  std::error_code type_ec;
  const bool is_directory = std::filesystem::is_directory(current, type_ec);
  if (type_ec) {
    return std::nullopt;
  }

  const bool is_regular_file = std::filesystem::is_regular_file(current, type_ec);
  if (type_ec) {
    return std::nullopt;
  }

  return ResolvedExistingPathInfo{
      .actual_path = std::move(current),
      .is_directory = is_directory,
      .is_regular_file = is_regular_file,
  };
}

std::optional<std::filesystem::path>
ResolveExistingPathCaseInsensitive(const char *candidate,
                                   const ExistingPathRequirement requirement) {
  const auto info = ResolveExistingPathInfoCaseInsensitive(candidate);
  if (!info.has_value() || !MatchesExistingPathRequirement(*info, requirement)) {
    return std::nullopt;
  }

  return info->actual_path;
}

std::optional<LooseResolvedPathMetadata> QueryLooseResolvedPathMetadata(const char *path) {
  const auto info = ResolveExistingPathInfoCaseInsensitive(path);
  if (!info.has_value()) {
    return std::nullopt;
  }

  const auto snapshot = LooseFileMetadataStore::Query(info->actual_path.string());
  if (!snapshot.has_value()) {
    return std::nullopt;
  }

  LooseResolvedPathMetadata metadata{};
  metadata.size = snapshot->size;
  metadata.translated_attributes = FileStack_TranslateWin32Attributes(snapshot->attribute_word);
  metadata.creation_time_ns_since_2000 = snapshot->creation_time_ns_since_2000;
  metadata.last_access_time_ns_since_2000 = snapshot->last_access_time_ns_since_2000;
  metadata.last_write_time_ns_since_2000 = snapshot->last_write_time_ns_since_2000;
  metadata.object_kind = info->is_directory ? 2u : info->is_regular_file ? 1u : 0u;
  metadata.non_directory_hint =
      (metadata.translated_attributes & kFileStackAttributeNonDirectory) != 0;
  metadata.raw_attributes = snapshot->attribute_word;
  return metadata;
}

std::string NormalizeDirectoryArchiveDisplayPath(const char *path) {
  std::array<char, 260> normalized{};
  if (!path) {
    return {};
  }

  int remaining = static_cast<int>(normalized.size());
  char *out = normalized.data();
  for (const char *cursor = path; remaining > 1 && *cursor != '\0'; ++cursor, --remaining) {
    *out++ = (*cursor == '/') ? '\\' : *cursor;
  }
  *out = '\0';

  if (normalized.front() == '\0') {
    return {};
  }

  const int length = static_cast<int>(std::strlen(normalized.data()));
  const char trailing = length > 0 ? normalized[static_cast<std::size_t>(length - 1)] : '\0';
  if (trailing == '\\') {
    return normalized.data();
  }

  int separator_offset = length;
  if (trailing == '/' || trailing == '\\') {
    separator_offset -= 1;
  }
  if (separator_offset > 258) {
    separator_offset = 258;
  }

  normalized[static_cast<std::size_t>(separator_offset)] = '\\';
  normalized[static_cast<std::size_t>(separator_offset + 1)] = '\0';
  return normalized.data();
}

std::optional<std::string> ResolveLooseFileCaseInsensitive(const char *candidate) {
  const auto info = ResolveExistingPathInfoCaseInsensitive(candidate);
  if (!info.has_value() || !info->is_regular_file) {
    return std::nullopt;
  }

  const auto native = ToNativePath(candidate);
  const bool absolute = native.is_absolute();

  std::error_code ec;
  const auto cwd = std::filesystem::current_path(ec);
  if (ec) {
    return std::nullopt;
  }

  if (absolute) {
    return ToStormPathString(info->actual_path);
  }

  const auto relative = std::filesystem::relative(info->actual_path, cwd, ec);
  if (ec) {
    return std::nullopt;
  }
  return ToStormPathString(relative);
}

std::optional<std::int64_t> LookupLegacyManifestFileSize(const char *path) {
  const auto lookup_path = CanonicalizeStreamingManifestLookupPath(path);
  if (!lookup_path.has_value()) {
    return std::nullopt;
  }

  const auto &manifest_state = openwow::data::GetStreamingManifestState();
  for (const auto &entry : manifest_state.legacy_file_entries) {
    if (entry.lookup_path == *lookup_path) {
      return entry.size;
    }
  }

  return std::nullopt;
}

std::uint32_t ClassifyLegacyManifestFileKind(const std::int64_t file_size) {
  return file_size > 0 ? 1u : 2u;
}
bool QueryNativeWorkingDirectory(char *path, const int path_capacity) {
  if (!path || path_capacity <= 0) {
    return false;
  }

  path[0] = '\0';

  if (auto &hook = MutableFileSystemGetWorkingDirectoryHookForTests(); hook) {
    return hook(path, path_capacity);
  }

  std::error_code ec;
  const std::filesystem::path current = std::filesystem::current_path(ec);
  if (ec) {
    return false;
  }

  CopyStormPath(path, ToStormPathString(current).c_str(), path_capacity);
  return true;
}

bool SetNativeWorkingDirectory(const char *path) {
  if (auto &hook = MutableFileSystemSetWorkingDirectoryHookForTests(); hook) {
    return hook(path);
  }

  if (!path) {
    return false;
  }

  std::error_code ec;
  std::filesystem::current_path(ToNativePath(path), ec);
  return !ec;
}
bool FileSystem_MakeAbsolutePath(const char *source, char *resolved_path,
                                 const int resolved_path_capacity) {
  if (!source || !resolved_path || resolved_path_capacity <= 0) {
    return false;
  }

  const std::string utf8_absolute_path =
      ToUtf8FilesystemPathString(BuildAbsoluteFilesystemPath(source));
  openwow::core::NormalizePathToForwardSlashes(
      utf8_absolute_path.c_str(), resolved_path, resolved_path_capacity);

  const std::size_t source_length = std::strlen(source);
  if (source_length != 0 && source[source_length - 1] == '/') {
    openwow::core::EnsureTrailingStormPathSeparator(
        resolved_path, resolved_path_capacity, '/');
  }

  return true;
}

bool FileSystem_GetWorkingDirectory(char *path, const int path_capacity) {
  if (!path || path_capacity <= 0) {
    return false;
  }

  path[0] = '\0';
  auto *const callback_table = GetActiveFileStackCallbackTable();
  if (!callback_table) {
    return false;
  }

  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x14u));
  event_record.Write(0x74u, path);
  event_record.Write(0x78u, path_capacity);
  return DispatchFileStackEvent(callback_table, 0x14u, event_record);
}

bool FileSystem_GetWorkingDirectoryChecked(const int path_capacity, char *path) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_GetWorkingDirectory(path, path_capacity);
}

bool FileSystem_SetWorkingDirectory(const char *path) {
  auto *const callback_table = GetActiveFileStackCallbackTable();
  if (!callback_table) {
    return false;
  }

  FileStackEventRecord event_record;
  event_record.Write(0x00u, static_cast<std::uint32_t>(0x08u));
  event_record.Write(0x04u, path);
  return DispatchFileStackEvent(callback_table, 0x08u, event_record);
}

bool FileSystem_SetWorkingDirectoryChecked(const char *path) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_SetWorkingDirectory(path);
}

bool FileSystem_CanResolvePath(const char *path) {
  if (openwow::core::CanResolveStormFilesystemPath(path)) {
    return true;
  }

  openwow::data::SetCurrentStreamingStatusCode(8);
  return false;
}

FileSystemPathType FileSystem_GetPathType(const char *path) {
  switch (FileStack_QueryPathAttributes(GetActiveFileStackCallbackTable(), path)) {
  case 1u:
    return FileSystemPathType::kRegularFile;
  case 2u:
    return FileSystemPathType::kDirectory;
  default:
    return FileSystemPathType::kMissing;
  }
}

bool FileSystem_IsDirectory(const char *path) {
  return FileSystem_GetPathType(path) == FileSystemPathType::kDirectory;
}

bool FileSystem_IsRegularFile(const char *path) {
  return FileSystem_GetPathType(path) == FileSystemPathType::kRegularFile;
}

bool FileSystem_IsReadOnly(const char *path) {
  const auto metadata = QueryLooseResolvedPathMetadata(path);
  if (!metadata.has_value()) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  return (metadata->raw_attributes & kWin32FileAttributeReadOnly) != 0;
}

std::uint32_t OsGetFileAttributes(const char *path) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return 0;
  }

  FileStackPathMetadata metadata{};
  if (!FileStack_QueryPathMetadata(GetActiveFileStackCallbackTable(), path,
                                   &metadata)) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return FileStack_TranslateMetadataAttributesToWin32(
      static_cast<std::uint8_t>(metadata.translated_attributes));
}

namespace {

constexpr std::size_t kLegacyCreateDirectoryBoundedPrefixCapacity = 260u;

bool IsLegacyCreateDirectoryToleratedFailure(const std::error_code &ec) {
  return ec == std::make_error_code(std::errc::file_exists) ||
         ec == std::make_error_code(std::errc::permission_denied);
}

std::filesystem::path ResolveCreateDirectoryNativePath(const char *path) {
  if (const auto resolved = ResolvePathCaseInsensitiveForCreate(path);
      resolved.has_value()) {
    return *resolved;
  }

  return ToNativePath(path);
}

bool TryCreateSingleDirectoryLegacy(
    const std::filesystem::path &directory_path,
    const bool tolerate_existing_without_directory_check) {
  std::error_code ec;
  if (std::filesystem::create_directory(directory_path, ec)) {
    return true;
  }

  if (!IsLegacyCreateDirectoryToleratedFailure(ec)) {
    return false;
  }

  if (tolerate_existing_without_directory_check) {
    return true;
  }

  ec.clear();
  return std::filesystem::is_directory(directory_path, ec) && !ec;
}

std::string NormalizeCreateDirectoryPath(const char *path) {
  if (!path) {
    return {};
  }

  std::vector<char> normalized(std::strlen(path) + 1u);
  openwow::core::NormalizePathToBackslashes(
      path, normalized.data(), static_cast<int>(normalized.size()));
  return std::string(normalized.data());
}

}

bool FileSystem_CreateDirectory(const char *path, const bool recursive) {
  if (!path) {
    openwow::data::SetCurrentStreamingStatusCode(8);
    return false;
  }

  if (!recursive) {
    const std::filesystem::path directory_path =
        ResolveCreateDirectoryNativePath(path);
    if (directory_path.empty()) {
      return false;
    }

    return TryCreateSingleDirectoryLegacy(directory_path, true);
  }

  const std::string normalized_path = NormalizeCreateDirectoryPath(path);
  if (normalized_path.empty()) {
    return true;
  }

  std::size_t prefix_end = 0;
  while (prefix_end < normalized_path.size()) {
    const std::size_t separator = normalized_path.find('\\', prefix_end);
    const std::size_t prefix_length =
        separator == std::string::npos ? normalized_path.size() : separator + 1u;
    const std::string prefix = normalized_path.substr(0, prefix_length);
    std::array<char, kLegacyCreateDirectoryBoundedPrefixCapacity> bounded_prefix{};
    openwow::core::SStrCopy(bounded_prefix.data(), prefix.c_str(), bounded_prefix.size());
    const std::filesystem::path directory_path =
        ResolveCreateDirectoryNativePath(bounded_prefix.data());
    if (directory_path.empty() ||
        !TryCreateSingleDirectoryLegacy(directory_path, false)) {
      openwow::data::SetCurrentStreamingStatusCode(8);
      return false;
    }

    if (separator == std::string::npos) {
      break;
    }

    prefix_end = separator + 1u;
  }

  return true;
}

bool OsCreateDirectory(const char *path, const bool recursive) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return IOUnitContainer_CreateDirectory(path, recursive);
}

bool OsMoveFile(const char *source_path, const char *destination_path) {
  if (source_path == nullptr || destination_path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return IOUnitContainer_MovePath(source_path, destination_path);
}

bool OsCopyFile(const char *source_path, const char *destination_path,
                const bool fail_if_exists) {
  if (source_path == nullptr || destination_path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return IOUnitContainer_CopyPath(source_path, destination_path, !fail_if_exists);
}

bool FileSystem_SetPathAttributes(const char *path, const std::uint32_t file_attributes) {
  if (!path || *path == '\0') {
    return false;
  }

  const auto resolved = ResolveExistingPathInfoCaseInsensitive(path);
  if (!resolved.has_value()) {
    return false;
  }

#if defined(_WIN32)
  return ::SetFileAttributesW(resolved->actual_path.c_str(),
                              static_cast<DWORD>(file_attributes & 0xFFFFu)) != FALSE;
#else
  LooseFileMetadataStore::StoreAttributes(
      resolved->actual_path,
      static_cast<std::uint16_t>(file_attributes & kWin32SettableFileAttributes));
  return true;
#endif
}

namespace {

constexpr std::uint32_t kOsFileOpenCreateNewFlags = 0x0C00u;
constexpr std::uint32_t kOsFileOpenCreateAlwaysFlags = 0x0400u;
constexpr std::uint32_t kOsFileOpenOpenExistingFlags = 0x1000u;
constexpr std::uint32_t kOsFileOpenOpenAlwaysFlags = 0x0200u;
constexpr std::uint32_t kOsFileOpenTruncateExistingFlags = 0x0100u;
constexpr std::uint32_t kOsFileOpenSkipAttributeMask = 0x0080u;

std::uint32_t EncodeOSFileOpenFlags(const int desired_access, const char share_mode,
                                    const int creation_disposition) {
  std::uint32_t flags = desired_access < 0 ? 0x1u : 0u;
  if ((desired_access & static_cast<int>(0x40000000u)) != 0) {
    flags |= 0x2u;
  }
  if ((share_mode & 0x1) != 0) {
    flags |= 0x4u;
  }
  if ((share_mode & 0x2) != 0) {
    flags |= 0x8u;
  }

  switch (creation_disposition) {
  case 1:
    flags |= kOsFileOpenCreateNewFlags;
    break;
  case 2:
    flags |= kOsFileOpenCreateAlwaysFlags;
    break;
  case 3:
    flags |= kOsFileOpenOpenExistingFlags;
    break;
  case 4:
    flags |= kOsFileOpenOpenAlwaysFlags;
    break;
  case 5:
    flags |= kOsFileOpenTruncateExistingFlags;
    break;
  default:
    break;
  }

  return flags;
}

}

bool OsSetFileAttributes(const char *path, const std::uint32_t file_attributes) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_SetPathAttributes(path, file_attributes);
}

int OS_FileOpen(const char *path, const int desired_access, const char share_mode,
                const int creation_disposition, const std::uint32_t file_attributes) {
  if (path == nullptr || desired_access == 0 || creation_disposition < 1
      || creation_disposition > 5) {
    return 0;
  }

  int handle = 0;
  if (!IOUnitContainer_CreateFileHandle(
          path, EncodeOSFileOpenFlags(desired_access, share_mode, creation_disposition),
          &handle)) {
    return 0;
  }

  if ((file_attributes & kOsFileOpenSkipAttributeMask) == 0u) {
    (void)FileStack_SetPathAttributes(GetActiveFileStackCallbackTable(), path,
                                      file_attributes);
  }

  return handle;
}

bool OsRemoveDirectory(const char *path) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_RemoveDirectory(path);
}

bool FileSystem_DeleteFile(const char *path) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, std::uint32_t{0x6Cu});
  event_record.Write(0x04u, path);
  return DispatchFileStackEvent(GetActiveFileStackCallbackTable(), 0x6Cu, event_record);
}

bool FileSystem_RemoveDirectory(const char *path) {
  FileStackEventRecord event_record;
  event_record.Write(0x00u, std::uint32_t{0x58u});
  event_record.Write(0x04u, path);
  event_record.Write(0x7Cu, std::uint8_t{0});
  return DispatchFileStackEvent(GetActiveFileStackCallbackTable(), 0x58u, event_record);
}

bool OsDeleteFile(const char *path) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_DeleteFile(path);
}

bool OsRemoveDirectoryTree(const char *path, const bool reset_file_attributes_before_delete) {
  if (path == nullptr) {
    openwow::platform::SetPlatformLastError(87);
    return false;
  }

  return FileSystem_RemoveDirectoryTree(path, reset_file_attributes_before_delete);
}

constexpr std::uint32_t kWin32CopyFileChunkSize = 0x00A00008u;
constexpr int kWin32CopyPathCapacity = 1024;
constexpr std::uint32_t kRecursiveDeleteFileAttributes = 0x20u;
constexpr int kRecursiveDeletePathCapacity = 1024;

std::array<char, kWin32CopyPathCapacity> NormalizeWin32CopyPath(const char *path) {
  std::array<char, kWin32CopyPathCapacity> normalized{};
  openwow::core::NormalizePathToBackslashes(path, normalized.data(),
                                            static_cast<int>(normalized.size()));
  return normalized;
}

std::optional<std::filesystem::path>
ResolvePathCaseInsensitiveForCreate(const char *candidate) {
  if (!candidate || !*candidate) {
    return std::nullopt;
  }

  const auto native = ToNativePath(candidate);
  const bool absolute = native.is_absolute();

  std::error_code ec;
  std::filesystem::path current =
      absolute ? native.root_path() : std::filesystem::current_path(ec);
  if (ec) {
    return std::nullopt;
  }

  bool missing_started = false;
  const auto remainder = absolute ? native.relative_path() : native;
  for (const auto &component_path : remainder) {
    const auto component = component_path.string();
    if (component.empty() || component == ".") {
      continue;
    }

    if (component == "..") {
      current = current.parent_path();
      continue;
    }

    if (!missing_started) {
      if (const auto matched = ResolveDirectoryEntryCaseInsensitive(current, component);
          matched.has_value()) {
        current = *matched;
        continue;
      }
      missing_started = true;
    }

    current /= component_path;
  }

  return current.lexically_normal();
}

bool CopyLooseFilePath(const char *source_path, const char *destination_path,
                       const bool overwrite_existing) {
  const auto normalized_source = NormalizeWin32CopyPath(source_path);
  const auto normalized_destination = NormalizeWin32CopyPath(destination_path);

  const auto source_info = ResolveExistingPathInfoCaseInsensitive(normalized_source.data());
  if (!source_info.has_value() || !source_info->is_regular_file) {
    openwow::core::StormSetLastError(2);
    return false;
  }

  const auto destination_info =
      ResolveExistingPathInfoCaseInsensitive(normalized_destination.data());
  if (destination_info.has_value() && !destination_info->is_regular_file) {
    openwow::core::StormSetLastError(4);
    return false;
  }
  if (!overwrite_existing && destination_info.has_value()) {
    openwow::core::StormSetLastError(4);
    return false;
  }

  const auto destination_path_native =
      destination_info.has_value()
          ? std::optional<std::filesystem::path>{destination_info->actual_path}
          : ResolvePathCaseInsensitiveForCreate(normalized_destination.data());
  if (!destination_path_native.has_value()) {
    openwow::core::StormSetLastError(4);
    return false;
  }

  std::ifstream source_stream(source_info->actual_path, std::ios::binary);
  if (!source_stream.is_open()) {
    openwow::core::StormSetLastError(2);
    return false;
  }

  std::ofstream destination_stream(*destination_path_native,
                                   std::ios::binary | std::ios::trunc);
  if (!destination_stream.is_open()) {
    openwow::core::StormSetLastError(4);
    return false;
  }

  source_stream.seekg(0, std::ios::end);
  const auto size_position = source_stream.tellg();
  if (size_position < 0) {
    destination_stream.close();
    source_stream.close();
    std::error_code remove_ec;
    (void)std::filesystem::remove(*destination_path_native, remove_ec);
    return false;
  }
  source_stream.seekg(0, std::ios::beg);

  std::uint64_t remaining = static_cast<std::uint64_t>(size_position);
  std::vector<char> buffer(static_cast<std::size_t>(
      std::min<std::uint64_t>(remaining, kWin32CopyFileChunkSize)));

  bool ok = true;
  while (remaining != 0) {
    const auto chunk_size =
        static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
    source_stream.read(buffer.data(), static_cast<std::streamsize>(chunk_size));
    if (source_stream.gcount() != static_cast<std::streamsize>(chunk_size)) {
      ok = false;
      break;
    }

    destination_stream.write(buffer.data(), static_cast<std::streamsize>(chunk_size));
    if (!destination_stream) {
      ok = false;
      break;
    }

    remaining -= chunk_size;
  }

  destination_stream.close();
  source_stream.close();
  if (!ok) {
    std::error_code remove_ec;
    (void)std::filesystem::remove(*destination_path_native, remove_ec);
  }
  return ok;
}

namespace {

bool FileSystem_RemoveDirectoryTreeChildren(const char *directory_path,
                                            const bool reset_file_attributes_before_delete,
                                            bool *out_success);

bool FileSystem_RemoveDirectoryTreeEntry(const FileSystemDirectoryEntry &entry,
                                         const bool reset_file_attributes_before_delete,
                                         bool *out_success) {
  if (!out_success) {
    return true;
  }

  std::array<char, kRecursiveDeletePathCapacity> child_path{};
  openwow::core::JoinStormPathBounded(child_path.data(), static_cast<int>(child_path.size()),
                                      entry.directory_path, entry.entry_name);

  if (entry.is_directory) {
    openwow::core::EnsureTrailingStormPathSeparator(
        child_path.data(), static_cast<int>(child_path.size()));
    (void)FileSystem_RemoveDirectoryTreeChildren(child_path.data(),
                                                 reset_file_attributes_before_delete,
                                                 out_success);
    if (!FileSystem_RemoveDirectory(child_path.data())) {
      *out_success = false;
    }
    return true;
  }

  if (reset_file_attributes_before_delete) {
    (void)FileSystem_SetPathAttributes(child_path.data(), kRecursiveDeleteFileAttributes);
  }

  if (openwow::core::StreamingStorage::Instance().HasManifestEntryForPath(child_path.data())) {
    return true;
  }

  const auto resolved_file = ResolveExistingPathCaseInsensitive(
      child_path.data(), ExistingPathRequirement::kFileOnly);
  std::error_code remove_ec;
  if (!resolved_file.has_value() ||
      !std::filesystem::remove(*resolved_file, remove_ec) || remove_ec) {
    *out_success = false;
  }

  return true;
}

bool FileSystem_RemoveDirectoryTreeChildren(const char *directory_path,
                                            const bool reset_file_attributes_before_delete,
                                            bool *out_success) {
  return FileSystem_EnumerateDirectoryEntries(
      directory_path,
      [&](const FileSystemDirectoryEntry &entry) {
        return FileSystem_RemoveDirectoryTreeEntry(entry, reset_file_attributes_before_delete,
                                                   out_success);
      });
}

}

bool FileSystem_RemoveDirectoryTree(const char *path,
                                    const bool reset_file_attributes_before_delete) {
  if (FileSystem_GetPathType(path) != FileSystemPathType::kDirectory) {
    return false;
  }

  std::array<char, kRecursiveDeletePathCapacity> resolved_directory{};
  if (!ResolveExistingPathAbsolute(path, resolved_directory.data(),
                                   static_cast<int>(resolved_directory.size()),
                                   ExistingPathRequirement::kDirectoryOnly)) {
    return false;
  }

  openwow::core::EnsureTrailingStormPathSeparator(
      resolved_directory.data(), static_cast<int>(resolved_directory.size()));

  bool success = true;
  if (!FileSystem_RemoveDirectoryTreeChildren(resolved_directory.data(),
                                              reset_file_attributes_before_delete, &success)) {
    return false;
  }

  if (!success) {
    return false;
  }

  return FileSystem_RemoveDirectory(resolved_directory.data());
}

bool SFileFindFiles(const char *path, const char *pattern,
                    const std::function<bool(const SFileFindData &)> &callback,
                    const bool include_hidden) {
  if (!path || !pattern || !callback) {
    return false;
  }

  std::array<char, 260> search_path{};
  openwow::core::SStrCopy(search_path.data(), path, search_path.size());

  char *last_separator = nullptr;
  for (char *cursor = search_path.data(); *cursor != '\0'; ++cursor) {
    if (*cursor == '\\' || *cursor == '/') {
      last_separator = cursor;
    }
  }
  if (last_separator) {
    *last_separator = '\0';
  }

  std::array<char, 260> resolved_directory{};
  if (!FileSystem_MakeAbsolutePath(search_path.data(), resolved_directory.data(),
                                   static_cast<int>(resolved_directory.size()))) {
    return false;
  }

  return FileSystem_EnumerateDirectoryEntries(
      resolved_directory.data(), [&](const FileSystemDirectoryEntry &entry) {
        if (!entry.entry_name ||
            !openwow::core::SStrWildcardMatch(entry.entry_name, pattern)) {
          return true;
        }

        std::array<char, 260> full_path{};
        openwow::core::JoinStormPathBounded(
            full_path.data(), static_cast<int>(full_path.size()),
            resolved_directory.data(), entry.entry_name);

        FileStackPathMetadata metadata{};
        if (!FileStack_QueryPathMetadata(GetActiveFileStackCallbackTable(),
                                         full_path.data(), &metadata)) {
          return true;
        }

        if (!include_hidden &&
            (metadata.translated_attributes & kFileStackAttributeHidden) != 0) {
          return true;
        }

        std::uint32_t callback_attributes = 0;
        if ((metadata.translated_attributes & kFileStackAttributeReadOnly) != 0) {
          callback_attributes |= kWin32FileAttributeReadOnly;
        }
        if ((metadata.translated_attributes & kFileStackAttributeHidden) != 0) {
          callback_attributes |= kWin32FileAttributeHidden;
        }
        if ((metadata.translated_attributes & kFileStackAttributeDirectory) != 0) {
          callback_attributes |= kWin32FileAttributeDirectory;
        }

        const SFileFindData find_data{
            .file_size_low = metadata.file_size_low,
            .file_attributes = callback_attributes,
            .entry_name = entry.entry_name,
        };
        return !callback(find_data);
      });
}

bool FileSystem_EnumerateDirectoryEntries(
    const char *path, const std::function<bool(const FileSystemDirectoryEntry &)> &callback) {
  if (!path || !callback) {
    return false;
  }

  char resolved_directory[260] = {};
  if (!ResolveExistingPathAbsolute(path, resolved_directory,
                                   static_cast<int>(sizeof(resolved_directory)),
                                   ExistingPathRequirement::kDirectoryOnly)) {
    return false;
  }

  std::error_code ec;
  const std::filesystem::path directory_path = ToNativePath(resolved_directory);
  for (std::filesystem::directory_iterator it(directory_path, ec), end; !ec && it != end;
       it.increment(ec)) {
    const std::string entry_name = ToStormPathString(it->path().filename());
    if (entry_name.empty() || entry_name == "." || entry_name == "..") {
      continue;
    }

    std::error_code type_ec;
    const FileSystemDirectoryEntry entry{
        .directory_path = path,
        .entry_name = entry_name.c_str(),
        .is_directory = it->is_directory(type_ec) && !type_ec,
    };
    if (!callback(entry)) {
      return true;
    }
  }

  return !ec;
}

void SetFileSystemGetWorkingDirectoryHookForTests(
    std::function<bool(char *, int)> hook) {
  MutableFileSystemGetWorkingDirectoryHookForTests() = std::move(hook);
}

void ResetFileSystemGetWorkingDirectoryHookForTests() {
  MutableFileSystemGetWorkingDirectoryHookForTests() = {};
}

void SetFileSystemSetWorkingDirectoryHookForTests(std::function<bool(const char *)> hook) {
  MutableFileSystemSetWorkingDirectoryHookForTests() = std::move(hook);
}

void ResetFileSystemSetWorkingDirectoryHookForTests() {
  MutableFileSystemSetWorkingDirectoryHookForTests() = {};
}

}

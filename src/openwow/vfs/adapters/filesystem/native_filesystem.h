#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace openwow::vfs {

enum class FileSystemPathType : std::int32_t {
  kMissing = 0,
  kRegularFile = 1,
  kDirectory = 2,
};

enum class ExistingPathRequirement : std::uint8_t {
  kFileOnly,
  kDirectoryOnly,
  kFileOrDirectory,
};

struct FileSystemDirectoryEntry {
  const char *directory_path{nullptr};
  const char *entry_name{nullptr};
  bool is_directory{false};
};

struct SFileFindData {
  std::uint32_t file_size_low{0};
  std::uint32_t file_attributes{0};
  const char *entry_name{nullptr};
};

struct ResolvedExistingPathInfo {
  std::filesystem::path actual_path;
  bool is_directory = false;
  bool is_regular_file = false;
};

struct LooseResolvedPathMetadata {
  std::uint64_t size = 0;
  std::uint32_t translated_attributes = 0;
  std::int64_t creation_time_ns_since_2000 = 0;
  std::int64_t last_access_time_ns_since_2000 = 0;
  std::int64_t last_write_time_ns_since_2000 = 0;
  std::uint32_t object_kind = 0;
  bool non_directory_hint = false;
  std::uint16_t raw_attributes = 0;
};

std::filesystem::path ToNativePath(const char *raw_path);
std::string ToStormPathString(const std::filesystem::path &path);
std::string ToUtf8FilesystemPathString(const std::filesystem::path &path);
std::filesystem::path BuildAbsoluteFilesystemPath(const char *source);
std::optional<ResolvedExistingPathInfo> ResolveExistingPathInfoCaseInsensitive(
    const char *candidate);
std::optional<std::filesystem::path> ResolveExistingPathCaseInsensitive(
    const char *candidate, ExistingPathRequirement requirement);
std::optional<std::filesystem::path> ResolvePathCaseInsensitiveForCreate(
    const char *candidate);
std::optional<LooseResolvedPathMetadata> QueryLooseResolvedPathMetadata(const char *path);
std::optional<std::string> ResolveLooseFileCaseInsensitive(const char *candidate);
std::string NormalizeDirectoryArchiveDisplayPath(const char *path);
std::optional<std::int64_t> LookupLegacyManifestFileSize(const char *path);
std::uint32_t ClassifyLegacyManifestFileKind(std::int64_t file_size);
bool QueryNativeWorkingDirectory(char *path, int path_capacity);
bool SetNativeWorkingDirectory(const char *path);
bool FileSystem_MakeAbsolutePath(const char *source, char *resolved_path,
                                 int resolved_path_capacity);
int ResolveExistingPathAbsolute(const char *source, char *resolved_path,
                                int resolved_path_capacity,
                                ExistingPathRequirement requirement);
bool FileSystem_GetWorkingDirectory(char *path, int path_capacity);
bool FileSystem_GetWorkingDirectoryChecked(int path_capacity, char *path);
bool FileSystem_SetWorkingDirectory(const char *path);
bool FileSystem_SetWorkingDirectoryChecked(const char *path);
bool FileSystem_CanResolvePath(const char *path);
FileSystemPathType FileSystem_GetPathType(const char *path);
bool FileSystem_IsDirectory(const char *path);
bool FileSystem_IsRegularFile(const char *path);
bool FileSystem_IsReadOnly(const char *path);
std::uint32_t OsGetFileAttributes(const char *path);
bool FileSystem_CreateDirectory(const char *path, bool recursive);
bool OsCreateDirectory(const char *path, bool recursive);
bool OsMoveFile(const char *source_path, const char *destination_path);
bool OsCopyFile(const char *source_path, const char *destination_path, bool fail_if_exists);
bool FileSystem_SetPathAttributes(const char *path, std::uint32_t file_attributes);
bool OsSetFileAttributes(const char *path, std::uint32_t file_attributes);
int OS_FileOpen(const char *path, int desired_access, char share_mode,
                int creation_disposition, std::uint32_t file_attributes);
bool FileSystem_DeleteFile(const char *path);
bool FileSystem_RemoveDirectory(const char *path);
bool OsRemoveDirectory(const char *path);
bool OsDeleteFile(const char *path);
bool FileSystem_RemoveDirectoryTree(const char *path,
                                    bool reset_file_attributes_before_delete);
bool OsRemoveDirectoryTree(const char *path, bool reset_file_attributes_before_delete);
bool FileSystem_EnumerateDirectoryEntries(
    const char *path,
    const std::function<bool(const FileSystemDirectoryEntry &)> &callback);
bool SFileFindFiles(const char *path, const char *pattern,
                    const std::function<bool(const SFileFindData &)> &callback,
                    bool include_hidden);
bool CopyLooseFilePath(const char *source_path, const char *destination_path,
                       bool overwrite_existing);

void SetFileSystemGetWorkingDirectoryHookForTests(std::function<bool(char *, int)> hook);
void ResetFileSystemGetWorkingDirectoryHookForTests();
void SetFileSystemSetWorkingDirectoryHookForTests(std::function<bool(const char *)> hook);
void ResetFileSystemSetWorkingDirectoryHookForTests();
void ResetLooseFileAttributeOverridesForTests();

}

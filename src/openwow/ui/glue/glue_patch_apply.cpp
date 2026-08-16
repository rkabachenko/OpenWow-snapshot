#include "openwow/ui/glue/glue_patch_apply.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_thread.h"
#include "openwow/core/storm_utils.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/adapters/filesystem/native_filesystem.h"
#include "openwow/vfs/retail/io_unit/io_unit_compat.h"

#if defined(_WIN32)

#include <windows.h>

#include <shellapi.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::ui::glue {

namespace {

constexpr std::int32_t kPatchArchivePriority = 100;
constexpr std::uint32_t kPatchArchiveFlags = 0u;
constexpr std::uint32_t kPatchWriteOpenFlags = 0x403u;
constexpr int kArchiveAuthenticated = 5;
constexpr std::size_t kPatchLineCapacity = 260u;
constexpr std::string_view kPatchArchiveName = "wow-patch.mpq";
constexpr std::string_view kPrepatchListName = "prepatch.lst";
constexpr std::string_view kExtractPrefix = "extract ";
constexpr std::string_view kExecutePrefix = "execute ";
constexpr std::string_view kDeletePrefix = "delete ";

std::optional<PatchDownloadApplyDependencies> &MutablePatchApplyOverride() {
  static std::optional<PatchDownloadApplyDependencies> override_dependencies;
  return override_dependencies;
}

std::string QueryWorkingDirectoryForPatchApply() {
  std::array<char, 260> path{};
  (void)openwow::vfs::FileSystem_GetWorkingDirectoryChecked(static_cast<int>(path.size()),
                                                            path.data());
  return std::string(path.data());
}

bool HasExplicitDependencies(const PatchDownloadApplyDependencies &dependencies) {
  return dependencies.get_working_directory || dependencies.get_startup_working_directory ||
         dependencies.set_working_directory || dependencies.open_archive ||
         dependencies.close_archive || dependencies.authenticate_archive ||
         dependencies.read_archive_file || dependencies.write_loose_file_best_effort ||
         dependencies.write_file_fork_best_effort || dependencies.finalize_extracted_file ||
         dependencies.is_regular_file || dependencies.is_directory || dependencies.delete_file ||
         dependencies.remove_directory || dependencies.remove_directory_tree ||
         dependencies.prepare_process_launch || dependencies.launch_process;
}

const PatchDownloadApplyDependencies &
ResolveRuntimeDependencies(const PatchDownloadApplyDependencies &dependencies) {
  if (HasExplicitDependencies(dependencies)) {
    return dependencies;
  }

  auto &override_dependencies = MutablePatchApplyOverride();
  if (override_dependencies.has_value()) {
    return *override_dependencies;
  }

  static const PatchDownloadApplyDependencies default_dependencies =
      MakeDefaultPatchDownloadApplyDependencies();
  return default_dependencies;
}

bool StartsWith(const std::string_view text, const std::string_view prefix) {
  return text.starts_with(prefix);
}

std::string CopyPatchListLineBounded(const char *begin, const char *end) {
  std::array<char, kPatchLineCapacity> buffer{};
  if (begin == nullptr || end == nullptr || end <= begin) {
    return std::string(buffer.data());
  }

  const auto source_length = static_cast<std::size_t>(end - begin);
  const auto copy_length = std::min(source_length, buffer.size() - 1u);
  std::memcpy(buffer.data(), begin, copy_length);
  return std::string(buffer.data());
}

std::string NextPatchListLine(const char **cursor) {
  if (cursor == nullptr || *cursor == nullptr) {
    return {};
  }

  const char *line_begin = *cursor;
  const char *line_end = line_begin;
  while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
    ++line_end;
  }

  std::string line = CopyPatchListLineBounded(line_begin, line_end);
  while (*line_end == '\r' || *line_end == '\n') {
    ++line_end;
  }

  *cursor = line_end;
  return line;
}

PatchWriteOpenResult WriteLooseFileRaw(const char *path, const std::string_view bytes) {
  int handle = 0;
  if (!openwow::vfs::IOUnitContainer_CreateFileHandle(path, kPatchWriteOpenFlags, &handle)) {
    return PatchWriteOpenResult::kOpenFailed;
  }

  std::uint32_t bytes_written = 0;
  static constexpr char kZeroByte = '\0';
  const void *buffer = bytes.empty() ? static_cast<const void *>(&kZeroByte)
                                     : static_cast<const void *>(bytes.data());
  (void)openwow::vfs::IOUnitContainer_WriteFileHandle_Wrapper(
      handle, buffer, static_cast<std::uint32_t>(bytes.size()), &bytes_written);
  (void)openwow::vfs::IOUnitContainer_CloseFileHandle(handle);
  return PatchWriteOpenResult::kOpened;
}

PatchWriteOpenResult WriteFileForkRawChecked(const char *path, const std::string_view bytes) {
  int handle = 0;
  if (!openwow::vfs::IOUnitContainer_CreateFileHandle(path, kPatchWriteOpenFlags, &handle)) {
    return PatchWriteOpenResult::kOpenFailed;
  }

  std::uint32_t bytes_written = 0;
  static constexpr char kZeroByte = '\0';
  const void *buffer = bytes.empty() ? static_cast<const void *>(&kZeroByte)
                                     : static_cast<const void *>(bytes.data());
  const int write_result = openwow::vfs::IOUnitContainer_WriteFileHandle_Wrapper(
      handle, buffer, static_cast<std::uint32_t>(bytes.size()), &bytes_written);
  (void)openwow::vfs::IOUnitContainer_CloseFileHandle(handle);
  return write_result != 0 && bytes_written == bytes.size()
             ? PatchWriteOpenResult::kOpened
             : PatchWriteOpenResult::kOpenFailed;
}

void CreateLooseFileParentDirectoryBestEffort(const char *path) {
  if (path == nullptr) {
    return;
  }

  const std::string_view path_view(path);
  const std::size_t separator = path_view.find_last_of("/\\");
  if (separator == std::string_view::npos) {
    return;
  }

  const std::string parent(path_view.substr(0, separator));
  if (!parent.empty()) {
    (void)openwow::vfs::FileSystem_CreateDirectory(parent.c_str(), true);
  }
}

PatchWriteOpenResult WriteLooseFileBestEffort(const char *path, const std::string_view bytes) {
  CreateLooseFileParentDirectoryBestEffort(path);
  return WriteLooseFileRaw(path, bytes);
}

PatchWriteOpenResult WriteMacFileForkBestEffort(const char *path, const std::string_view bytes,
                                                const PatchFileFork fork) {
  if (path == nullptr || path[0] == '\0') {
    return PatchWriteOpenResult::kOpenFailed;
  }
  if (fork == PatchFileFork::kData) {
    return WriteFileForkRawChecked(path, bytes);
  }

#if defined(__APPLE__)

  if (!openwow::vfs::FileSystem_IsRegularFile(path) &&
      WriteFileForkRawChecked(path, {}) == PatchWriteOpenResult::kOpenFailed) {
    return PatchWriteOpenResult::kOpenFailed;
  }
  const std::string resource_fork_path = std::string(path) + "/..namedfork/rsrc";
  return WriteFileForkRawChecked(resource_fork_path.c_str(), bytes);
#else
  (void)bytes;
  return PatchWriteOpenResult::kOpenFailed;
#endif
}

void FinalizeExtractedPatchFile(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return;
  }

  std::error_code error;
  std::filesystem::path current = openwow::vfs::ToNativePath(path);
  const auto executable_permissions =
      std::filesystem::perms::owner_all | std::filesystem::perms::group_all |
      std::filesystem::perms::others_read | std::filesystem::perms::others_exec;
  std::filesystem::permissions(current, executable_permissions,
                               std::filesystem::perm_options::replace, error);

  const auto now = std::filesystem::file_time_type::clock::now();
  while (!current.empty()) {
    error.clear();
    std::filesystem::last_write_time(current, now, error);
    const std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
      break;
    }
    current = parent;
  }
}

void PreparePatchProcessLaunch() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("gxWindow") || cvars.GetCVarInt("gxWindow") != 0) {
    return;
  }

  (void)cvars.SetCVar("gxWindow", "1", true);
  openwow::core::ida::Console_Execute("gxRestart", true);
  (void)cvars.SetCVar("gxWindow", "0", true);
}

detail::PatchProcessLaunchRequest
ComposePatchProcessLaunchRequest(const std::uint32_t os_major_version, const char *path,
                                 const char *newline_delimited_arguments) {
  detail::PatchProcessLaunchRequest request;
  if (path == nullptr || path[0] == '\0') {
    return request;
  }

  request.executable_path = path;
  request.shell_execute_parameters =
      newline_delimited_arguments != nullptr ? newline_delimited_arguments : "";
  if (os_major_version >= 6u) {
    request.kind = detail::PatchProcessLaunchKind::kRunAs;
    return request;
  }

  request.spawn_command_line = openwow::core::storm_thread_detail::BuildQuotedProcessCommandLine(
      path, request.shell_execute_parameters.c_str());
  return request;
}

bool LaunchPatchProcess(const char *path) {
  constexpr const char *kEmptyArguments = "";

#if defined(_WIN32)
  OSVERSIONINFOA version_info{};
  version_info.dwOSVersionInfoSize = sizeof(version_info);
  (void)::GetVersionExA(&version_info);
#else
  constexpr std::uint32_t kNonWindowsVersionMajor = 0;
#endif

  const detail::PatchProcessLaunchRequest request =
#if defined(_WIN32)
      ComposePatchProcessLaunchRequest(version_info.dwMajorVersion, path, kEmptyArguments);
#else
      ComposePatchProcessLaunchRequest(kNonWindowsVersionMajor, path, kEmptyArguments);
#endif
  if (request.executable_path.empty()) {
    return false;
  }

#if defined(_WIN32)
  if (request.kind == detail::PatchProcessLaunchKind::kRunAs) {
    const std::string ansi_verb = openwow::core::Utf8ToCurrentCodePageString("runas");
    const std::string ansi_path =
        openwow::core::Utf8ToCurrentCodePageString(request.executable_path.c_str());
    const std::string ansi_parameters =
        openwow::core::Utf8ToCurrentCodePageString(request.shell_execute_parameters.c_str());
    const HINSTANCE instance = ::ShellExecuteA(nullptr, ansi_verb.c_str(), ansi_path.c_str(),
                                               ansi_parameters.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(instance) > 32;
  }
#endif

#if defined(__APPLE__)
  constexpr const char *kLaunchServicesOpenTool = "/usr/bin/open";
  const std::string command_line =
      openwow::core::storm_thread_detail::BuildQuotedProcessCommandLine(
          kLaunchServicesOpenTool, request.executable_path.c_str());
  return openwow::core::SThread_SpawnProcess(kLaunchServicesOpenTool,
                                             command_line.c_str(), 0, 0) != 0;
#else
  return openwow::core::SThread_SpawnProcess(request.executable_path.c_str(),
                                             request.spawn_command_line.c_str(), 0, 0) != 0;
#endif
}

bool ReadArchiveFile(void *archive, const char *path, std::string *out_bytes,
                     const std::size_t extra_padding) {
  if (out_bytes == nullptr) {
    return false;
  }

  void *loaded_data = nullptr;
  int loaded_size = 0;
  if (!openwow::vfs::SFileReadFileToBuffer_SetLastError(archive, path, &loaded_data, &loaded_size,
                                                        extra_padding, 0, 0) ||
      loaded_data == nullptr) {
    return false;
  }

  out_bytes->assign(static_cast<const char *>(loaded_data), static_cast<std::size_t>(loaded_size));
  (void)openwow::vfs::SFileFreeLoadedData(loaded_data);
  return true;
}

class WorkingDirectoryRestoreScope {
public:
  WorkingDirectoryRestoreScope(const PatchDownloadApplyDependencies &dependencies,
                               std::string saved_path)
      : dependencies_(dependencies), saved_path_(std::move(saved_path)) {}

  ~WorkingDirectoryRestoreScope() {
    if (dependencies_.set_working_directory) {
      (void)dependencies_.set_working_directory(saved_path_);
    }
  }

private:
  const PatchDownloadApplyDependencies &dependencies_;
  std::string saved_path_;
};

bool HandleDeleteCommand(const char *path, bool *defer_delete_patch_archive,
                         const PatchDownloadApplyDependencies &dependencies) {
  if (path == nullptr || defer_delete_patch_archive == nullptr) {
    return true;
  }

  if (dependencies.is_regular_file && dependencies.is_regular_file(path)) {
    if (openwow::core::SStrCmpNoCase(path, kPatchArchiveName.data(), 0x7FFFFFFFu) == 0) {
      *defer_delete_patch_archive = true;
    } else if (dependencies.delete_file) {
      dependencies.delete_file(path);
    }
    return true;
  }

  if (dependencies.is_directory && dependencies.is_directory(path)) {
    constexpr std::string_view kApplicationBundleSuffix = ".app";
    const char *const bundle_suffix =
        openwow::core::SStrStrI(path, kApplicationBundleSuffix.data());
    const bool is_application_bundle =
        bundle_suffix != nullptr && bundle_suffix[kApplicationBundleSuffix.size()] == '\0';
    if (is_application_bundle && dependencies.remove_directory_tree) {
      dependencies.remove_directory_tree(path);
    } else if (dependencies.remove_directory) {
      dependencies.remove_directory(path);
    }
  }
  return true;
}

bool RunPrepatchList(void *archive, bool *defer_delete_patch_archive,
                     const PatchDownloadApplyDependencies &dependencies) {
  if (defer_delete_patch_archive == nullptr) {
    return false;
  }

  std::string prepatch_list;
  if (!dependencies.read_archive_file ||
      !dependencies.read_archive_file(archive, kPrepatchListName.data(), &prepatch_list, 1)) {
    return false;
  }

  *defer_delete_patch_archive = false;
  bool ok = true;
  const char *cursor = prepatch_list.c_str();
  while (*cursor != '\0' && ok) {
    const std::string line = NextPatchListLine(&cursor);

    if (StartsWith(line, kExtractPrefix)) {
      ok = ExtractPatchArchiveFileBestEffort(archive, line.c_str() + kExtractPrefix.size(),
                                             dependencies);
      continue;
    }

    if (StartsWith(line, kExecutePrefix)) {
      if (dependencies.prepare_process_launch) {
        dependencies.prepare_process_launch();
      }
      ok = dependencies.launch_process &&
           dependencies.launch_process(line.c_str() + kExecutePrefix.size());
      continue;
    }

    if (StartsWith(line, kDeletePrefix)) {
      ok = HandleDeleteCommand(line.c_str() + kDeletePrefix.size(), defer_delete_patch_archive,
                               dependencies);
    }
  }

  return ok;
}

}

detail::PatchProcessLaunchRequest
detail::BuildPatchProcessLaunchRequest(const std::uint32_t os_major_version, const char *path,
                                       const char *newline_delimited_arguments) {
  return ComposePatchProcessLaunchRequest(os_major_version, path, newline_delimited_arguments);
}

PatchDownloadApplyDependencies MakeDefaultPatchDownloadApplyDependencies() {
  return {
      .get_working_directory =
          [](std::string *out_path) {
            if (out_path == nullptr) {
              return false;
            }
            *out_path = QueryWorkingDirectoryForPatchApply();
            return !out_path->empty();
          },
      .get_startup_working_directory =
          []() { return openwow::data::GetCachedStartupWorkingDirectory(); },
      .set_working_directory =
          [](const std::string &path) {
            return openwow::vfs::FileSystem_SetWorkingDirectoryChecked(path.c_str());
          },
      .open_archive =
          [](const char *path, const std::int32_t priority, const std::uint32_t flags,
             void **out_archive) {
            if (openwow::vfs::SFileOpenArchiveWrapped(path, priority, flags, out_archive)) {
              return PatchArchiveOpenResult::kOpened;
            }

            return openwow::core::SErrGetLastError() == 2 ? PatchArchiveOpenResult::kNotFound
                                                          : PatchArchiveOpenResult::kOpenFailed;
          },
      .close_archive =
          [](void *archive) {
            if (archive != nullptr) {
              (void)openwow::vfs::SFileCloseArchiveWrapped(archive);
            }
          },
      .authenticate_archive =
          [](void *archive) {
            int result = 0;
            (void)openwow::vfs::SFileAuthenticateArchive(archive, &result, nullptr, 0, nullptr, 0);
            return result;
          },
      .read_archive_file = ReadArchiveFile,
      .write_loose_file_best_effort = WriteLooseFileBestEffort,
      .write_file_fork_best_effort = WriteMacFileForkBestEffort,
      .finalize_extracted_file = FinalizeExtractedPatchFile,
      .is_regular_file = openwow::vfs::FileSystem_IsRegularFile,
      .is_directory = openwow::vfs::FileSystem_IsDirectory,
      .delete_file = openwow::vfs::OsDeleteFile,
      .remove_directory = openwow::vfs::OsRemoveDirectory,
      .remove_directory_tree =
          [](const char *path) { (void)openwow::vfs::OsRemoveDirectoryTree(path, false); },
      .prepare_process_launch = PreparePatchProcessLaunch,
      .launch_process = LaunchPatchProcess,
  };
}

void SetPatchDownloadApplyDependenciesForTests(PatchDownloadApplyDependencies dependencies) {
  MutablePatchApplyOverride() = std::move(dependencies);
}

void ResetPatchDownloadApplyDependenciesForTests() {
  MutablePatchApplyOverride().reset();
  openwow::data::ResetCachedStartupWorkingDirectoryForTests();
}

bool ExtractPatchArchiveFileBestEffort(void *archive, const char *path,
                                       const PatchDownloadApplyDependencies &dependencies) {
  const auto &active_dependencies = ResolveRuntimeDependencies(dependencies);
  if (!active_dependencies.read_archive_file) {
    return false;
  }

  std::string bytes;
  if (!active_dependencies.read_archive_file(archive, path, &bytes, 0)) {
    return false;
  }

  constexpr std::string_view kDataForkPrefix = "DF.";
  constexpr std::string_view kResourceForkPrefix = "RF.";
  const std::string_view archive_path(path);
  if (archive_path.starts_with(kDataForkPrefix) ||
      archive_path.starts_with(kResourceForkPrefix)) {
    if (!active_dependencies.write_file_fork_best_effort) {
      return false;
    }
    const PatchFileFork fork = archive_path.starts_with(kDataForkPrefix)
                                   ? PatchFileFork::kData
                                   : PatchFileFork::kResource;
    const std::string target_path(archive_path.substr(kDataForkPrefix.size()));
    return active_dependencies.write_file_fork_best_effort(target_path.c_str(), bytes, fork) !=
           PatchWriteOpenResult::kOpenFailed;
  }

  if (!active_dependencies.write_loose_file_best_effort) {
    return false;
  }
  const PatchWriteOpenResult write_result =
      active_dependencies.write_loose_file_best_effort(path, bytes);
  if (write_result == PatchWriteOpenResult::kOpenFailed) {
    return false;
  }
  if (active_dependencies.finalize_extracted_file) {
    active_dependencies.finalize_extracted_file(path);
  }
  return true;
}

PatchDownloadApplyResult
ApplyDownloadedPatchWithResult(
    const PatchDownloadApplyDependencies &dependencies,
    const std::function<void(PatchDownloadApplyResult)> &before_failure_cleanup) {
  const auto &active_dependencies = ResolveRuntimeDependencies(dependencies);
  if (!active_dependencies.get_working_directory ||
      !active_dependencies.get_startup_working_directory ||
      !active_dependencies.set_working_directory || !active_dependencies.open_archive ||
      !active_dependencies.close_archive || !active_dependencies.authenticate_archive ||
      !active_dependencies.read_archive_file || !active_dependencies.write_loose_file_best_effort ||
      !active_dependencies.delete_file || !active_dependencies.launch_process) {
    constexpr PatchDownloadApplyResult kFailure =
        PatchDownloadApplyResult::kOpenOrAuthenticateFailed;
    if (before_failure_cleanup) {
      before_failure_cleanup(kFailure);
    }
    return kFailure;
  }

  std::string saved_working_directory;
  (void)active_dependencies.get_working_directory(&saved_working_directory);
  WorkingDirectoryRestoreScope restore_scope(active_dependencies, saved_working_directory);
  (void)active_dependencies.set_working_directory(
      active_dependencies.get_startup_working_directory());

  void *archive = nullptr;
  const PatchArchiveOpenResult open_result = active_dependencies.open_archive(
      kPatchArchiveName.data(), kPatchArchivePriority, kPatchArchiveFlags, &archive);
  if (open_result != PatchArchiveOpenResult::kOpened) {
    const PatchDownloadApplyResult result =
        open_result == PatchArchiveOpenResult::kNotFound
            ? PatchDownloadApplyResult::kArchiveMissing
            : PatchDownloadApplyResult::kOpenOrAuthenticateFailed;
    if (before_failure_cleanup) {
      before_failure_cleanup(result);
    }
    active_dependencies.delete_file(kPatchArchiveName.data());
    return result;
  }

  const int authentication_result = active_dependencies.authenticate_archive(archive);
  bool defer_delete_patch_archive = false;
  PatchDownloadApplyResult result = PatchDownloadApplyResult::kOpenOrAuthenticateFailed;
  if (authentication_result >= kArchiveAuthenticated) {
    result = RunPrepatchList(archive, &defer_delete_patch_archive, active_dependencies)
                 ? PatchDownloadApplyResult::kSuccess
                 : PatchDownloadApplyResult::kPrepatchListFailed;
  }

  active_dependencies.close_archive(archive);

  if (result != PatchDownloadApplyResult::kSuccess) {
    if (before_failure_cleanup) {
      before_failure_cleanup(result);
    }
    active_dependencies.delete_file(kPatchArchiveName.data());
  } else if (defer_delete_patch_archive) {
    active_dependencies.delete_file(kPatchArchiveName.data());
  }

  return result;
}

bool ApplyDownloadedPatch(const PatchDownloadApplyDependencies &dependencies) {
  return ApplyDownloadedPatchWithResult(dependencies) == PatchDownloadApplyResult::kSuccess;
}

}

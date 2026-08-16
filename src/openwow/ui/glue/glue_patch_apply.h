#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace openwow::ui::glue {

enum class PatchArchiveOpenResult : std::uint8_t {
  kOpenFailed = 0,
  kOpened = 1,
  kNotFound = 2,
};

enum class PatchWriteOpenResult : std::uint8_t {
  kOpenFailed = 0,
  kOpened = 1,
};

enum class PatchFileFork : std::uint8_t {
  kData = 0,
  kResource = 1,
};

enum class PatchDownloadApplyResult : std::uint8_t {
  kSuccess = 0,
  kArchiveMissing = 4,
  kOpenOrAuthenticateFailed = 5,
  kPrepatchListFailed = 6,
};

struct PatchDownloadApplyDependencies {
  std::function<bool(std::string *out_path)> get_working_directory;
  std::function<std::string()> get_startup_working_directory;
  std::function<bool(const std::string &path)> set_working_directory;
  std::function<PatchArchiveOpenResult(const char *path, std::int32_t priority, std::uint32_t flags,
                                       void **out_archive)>
      open_archive;
  std::function<void(void *archive)> close_archive;
  std::function<int(void *archive)> authenticate_archive;
  std::function<bool(void *archive, const char *path, std::string *out_bytes,
                     std::size_t extra_padding)>
      read_archive_file;
  std::function<PatchWriteOpenResult(const char *path, std::string_view bytes)>
      write_loose_file_best_effort;
  std::function<PatchWriteOpenResult(const char *path, std::string_view bytes,
                                     PatchFileFork fork)>
      write_file_fork_best_effort;
  std::function<void(const char *path)> finalize_extracted_file;
  std::function<bool(const char *path)> is_regular_file;
  std::function<bool(const char *path)> is_directory;
  std::function<void(const char *path)> delete_file;
  std::function<void(const char *path)> remove_directory;
  std::function<void(const char *path)> remove_directory_tree;
  std::function<void()> prepare_process_launch;
  std::function<bool(const char *path)> launch_process;
};

namespace detail {

enum class PatchProcessLaunchKind : std::uint8_t {
  kSpawnProcess = 0,
  kRunAs = 1,
};

struct PatchProcessLaunchRequest {
  PatchProcessLaunchKind kind = PatchProcessLaunchKind::kSpawnProcess;
  std::string executable_path;
  std::string shell_execute_parameters;
  std::string spawn_command_line;
};

PatchProcessLaunchRequest BuildPatchProcessLaunchRequest(std::uint32_t os_major_version,
                                                         const char *path,
                                                         const char *newline_delimited_arguments);

}

PatchDownloadApplyDependencies MakeDefaultPatchDownloadApplyDependencies();

void SetPatchDownloadApplyDependenciesForTests(PatchDownloadApplyDependencies dependencies);
void ResetPatchDownloadApplyDependenciesForTests();

bool ExtractPatchArchiveFileBestEffort(void *archive, const char *path,
                                       const PatchDownloadApplyDependencies &dependencies = {});

PatchDownloadApplyResult
ApplyDownloadedPatchWithResult(
    const PatchDownloadApplyDependencies &dependencies = {},
    const std::function<void(PatchDownloadApplyResult)> &before_failure_cleanup = {});

bool ApplyDownloadedPatch(const PatchDownloadApplyDependencies &dependencies = {});

}

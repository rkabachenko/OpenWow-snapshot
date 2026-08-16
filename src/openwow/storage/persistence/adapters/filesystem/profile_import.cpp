#include "openwow/storage/persistence/profile_import.h"

#include "openwow/platform/filesystem/filesystem.h"

#include <filesystem>

namespace openwow::storage::persistence {

ProfileImportResult ImportProfileCopyOnly(const std::string& source_path,
                                          const std::string& destination_path,
                                          const bool overwrite) {
  if (source_path.empty() || destination_path.empty()) {
    return {.status = ProfileImportStatus::kEmptyPath};
  }

  if (!openwow::platform::filesystem::RecursiveCopyDirectory(
          source_path, destination_path, overwrite)) {
    return {.status = ProfileImportStatus::kCopyFailed};
  }

  return {.status = ProfileImportStatus::kSuccess};
}

}

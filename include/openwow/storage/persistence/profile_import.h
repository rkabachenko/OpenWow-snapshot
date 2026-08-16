#pragma once

#include <string>

namespace openwow::storage::persistence {

enum class ProfileImportStatus {
  kSuccess,
  kEmptyPath,
  kCopyFailed,
};

struct ProfileImportResult {
  ProfileImportStatus status{ProfileImportStatus::kSuccess};

  [[nodiscard]] bool Succeeded() const noexcept {
    return status == ProfileImportStatus::kSuccess;
  }
};

[[nodiscard]] ProfileImportResult ImportProfileCopyOnly(
    const std::string& source_path, const std::string& destination_path,
    bool overwrite);

}

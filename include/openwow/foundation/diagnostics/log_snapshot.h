#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace openwow::diagnostics {

struct LogSnapshotResult {
  std::filesystem::path source;
  std::uintmax_t bytes{0};
  std::string error;
};

// Call from a background worker. Includes all entries queued before the call,
// then copies exactly that flushed prefix without stopping live logging.
// The destination must not exist; failures remove any partial copy.
[[nodiscard]] LogSnapshotResult CopyCurrentLogSnapshot(
    const std::filesystem::path& destination);

}

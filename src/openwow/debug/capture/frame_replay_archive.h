#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::debug {

enum class FrameReplayArchiveMode : std::uint8_t {
  kCreate,
  kAppend,
  kRead,
};

enum class FrameReplayArchiveErrorCode : std::uint8_t {
  kNone,
  kClosed,
  kInvalidOperation,
  kIo,
  kMalformedArchive,
  kUnsupportedArchive,
  kLimitExceeded,
  kEntryNotFound,
  kCompression,
  kChecksumMismatch,
  kInvalidPath,
  kRollbackFailed,
};

struct FrameReplayArchiveError {
  FrameReplayArchiveErrorCode code{FrameReplayArchiveErrorCode::kNone};
  std::string detail;

  explicit operator bool() const noexcept {
    return code != FrameReplayArchiveErrorCode::kNone;
  }
};

struct FrameReplayArchiveLimits {
  std::size_t max_entries{65'535};
  std::size_t max_entry_name_bytes{259};
  std::uint64_t max_entry_uncompressed_bytes{1ULL << 30U};
  std::uint64_t max_archive_bytes{0xFFFFFFFFULL};
};

enum class FrameReplayResourceKind : std::uint8_t {
  kVertexFormat,
  kShader,
  kBuffer,
  kTexture,
};

inline constexpr std::string_view kFrameReplayIdentifierEntry =
    "Identifier.frameReplay";

[[nodiscard]] std::string FrameReplayBatchEntryName(std::uint32_t batch);
[[nodiscard]] std::string FrameReplayResourceEntryName(
    FrameReplayResourceKind kind, std::uint32_t identifier);

class FrameReplayArchive {
 public:
  [[nodiscard]] static std::optional<FrameReplayArchive> Open(
      const std::filesystem::path& path, FrameReplayArchiveMode mode,
      FrameReplayArchiveError* error = nullptr,
      FrameReplayArchiveLimits limits = {});

  ~FrameReplayArchive();
  FrameReplayArchive(FrameReplayArchive&&) noexcept;
  FrameReplayArchive& operator=(FrameReplayArchive&&) noexcept;
  FrameReplayArchive(const FrameReplayArchive&) = delete;
  FrameReplayArchive& operator=(const FrameReplayArchive&) = delete;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] FrameReplayArchiveMode mode() const noexcept;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

  [[nodiscard]] std::vector<std::string> EntryNames() const;
  [[nodiscard]] bool Contains(std::string_view name) const noexcept;

  [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReadEntry(
      std::string_view name, FrameReplayArchiveError* error = nullptr) const;
  [[nodiscard]] bool WriteEntry(
      std::string_view name, std::span<const std::uint8_t> bytes,
      FrameReplayArchiveError* error = nullptr);

  [[nodiscard]] bool Close(FrameReplayArchiveError* error = nullptr);

 private:
  struct Impl;

  explicit FrameReplayArchive(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}

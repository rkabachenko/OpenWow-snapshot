#pragma once

#include "openwow/core/md5.h"
#include "openwow/net/login_patch_download_session.h"
#include "openwow/foundation/text/ascii.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>

namespace openwow::net {

class LoginInlineDownloadCache : public LoginPatchDownloadSession {
 public:
  static constexpr std::uint32_t kPartialTrailerMagic = 0x44496E66u;
  static constexpr std::uint32_t kChunkFlushThreshold = 0x100000u;
  static constexpr std::size_t kDigestSize = 16;

  enum class State : std::uint32_t {
    kInProgress = 0,
    kComplete = 2,
    kFailed = 3,
  };

  enum class StartDisposition {
    kTransferRequired,
    kAlreadyComplete,
    kFailed,
  };

  struct StartResult {
    StartDisposition disposition{StartDisposition::kFailed};
    std::uint64_t resume_offset{0};
    bool uses_partial_file{false};
  };

  LoginInlineDownloadCache() = default;
  ~LoginInlineDownloadCache();

  LoginInlineDownloadCache(const LoginInlineDownloadCache&) = delete;
  LoginInlineDownloadCache& operator=(const LoginInlineDownloadCache&) = delete;

  StartResult Begin(const std::filesystem::path& target_path,
                    std::span<const std::uint8_t, kDigestSize> expected_digest,
                    std::uint64_t expected_size);

  bool AppendChunk(std::span<const std::uint8_t> bytes);
  void AbortTransfer() override;
  void Close(bool delete_file) override;
  void FinalizeSuccess() override;

  [[nodiscard]] State state() const;
  [[nodiscard]] std::uint32_t result_code() const;
  [[nodiscard]] std::uint64_t current_size() const override;
  [[nodiscard]] std::uint64_t expected_size() const;
  [[nodiscard]] bool uses_partial_file() const;
  [[nodiscard]] double progress_ratio() const override;
  [[nodiscard]] LoginPatchDownloadState download_state() const override;
  [[nodiscard]] LoginPatchDownloadFailureInfo failure_info() const override;
  [[nodiscard]] std::optional<bool> DispatchInlineChunk(
      std::span<const std::uint8_t> bytes) override;

 private:
  struct PartialTrailer {
    std::uint32_t magic = kPartialTrailerMagic;
    std::uint32_t reserved = 0;
    std::array<std::uint8_t, kDigestSize> digest{};
    std::uint64_t current_size = 0;
    std::uint64_t expected_size = 0;
  };
  static_assert(sizeof(PartialTrailer) == 40);

  [[nodiscard]] std::filesystem::path PartialPath() const;
  [[nodiscard]] bool OpenExistingFile(const std::filesystem::path& path);
  [[nodiscard]] bool CreateSizedFile(const std::filesystem::path& path,
                                     std::uint64_t size);
  [[nodiscard]] bool WriteTrailerLocked();
  [[nodiscard]] bool ValidateFileDigestLocked(std::uint64_t size);
  [[nodiscard]] bool ReadPartialTrailerLocked(PartialTrailer* trailer);
  void CloseFileLocked();
  void MarkFailedLocked(std::uint32_t result_code);

  mutable std::mutex mutex_;
  std::filesystem::path target_path_;
  std::array<std::uint8_t, kDigestSize> expected_digest_{};
  std::fstream file_;
  std::uint64_t current_size_ = 0;
  std::uint64_t expected_size_ = 0;
  std::uint32_t bytes_since_trailer_flush_ = 0;
  std::uint32_t result_code_ = 0;

  std::uint64_t auxiliary_value_ = 0;
  State state_ = State::kInProgress;
  bool uses_partial_file_ = false;
};

bool DispatchInlineDownloadChunk(LoginInlineDownloadCache* download,
                                 std::span<const std::uint8_t> bytes);

struct LoginInlineDownloadBootstrapResult {
  std::filesystem::path target_path;
  LoginInlineDownloadCache::StartResult start{};
  bool has_active_download{false};
};

LoginInlineDownloadBootstrapResult BeginNamedInlineDownload(
    const std::filesystem::path& client_root,
    const std::filesystem::path& relative_target_path,
    std::span<const std::uint8_t, LoginInlineDownloadCache::kDigestSize>
        expected_digest,
    std::uint64_t expected_size,
    std::unique_ptr<LoginInlineDownloadCache>* active_download);

inline LoginInlineDownloadBootstrapResult StartPatchInlineDownload(
    const std::filesystem::path& client_root,
    std::string_view raw_filename,
    std::span<const std::uint8_t, LoginInlineDownloadCache::kDigestSize>
        expected_digest,
    const std::uint64_t expected_size,
    std::unique_ptr<LoginInlineDownloadCache>* active_download) {
  const std::filesystem::path relative_target =
      openwow::text::EqualsIgnoreCaseAscii(raw_filename, "Patch")
          ? std::filesystem::path("wow-patch.mpq")
          : std::filesystem::path(raw_filename);
  return BeginNamedInlineDownload(client_root,
                                  relative_target,
                                  expected_digest,
                                  expected_size,
                                  active_download);
}

inline LoginInlineDownloadBootstrapResult StartSurveyInlineDownload(
    const std::filesystem::path& client_root,
    std::span<const std::uint8_t, LoginInlineDownloadCache::kDigestSize>
        expected_digest,
    const std::uint64_t expected_size,
    std::unique_ptr<LoginInlineDownloadCache>* active_download) {
  return BeginNamedInlineDownload(client_root,
                                  std::filesystem::path("Cache") /
                                      "Survey.mpq",
                                  expected_digest,
                                  expected_size,
                                  active_download);
}

}

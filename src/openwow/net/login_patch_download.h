#pragma once

#include "openwow/net/login_inline_download_cache.h"
#include "openwow/net/login_patch_download_session.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace openwow::net {

struct LoginPatchDownloadSnapshot {
  bool has_active_download{false};
  LoginPatchDownloadState state{LoginPatchDownloadState::kFailed};
  std::uint32_t result_code{0};
  double progress_ratio{0.0};
};

struct LoginPatchDownloadRestartRequest {
  std::string account_name;
  std::string password;
};

struct PatchDownloadBootstrapResult {
  bool started_inline_download{false};
  bool started_manifest_download{false};
  bool queued_login_restart{false};
  std::filesystem::path target_path;
  LoginInlineDownloadCache::StartResult start{};
};

class LoginPatchDownloadBridge {
 public:
  static LoginPatchDownloadBridge& Get();

  void ReplaceActiveDownload(std::unique_ptr<LoginPatchDownloadSession> download);
  [[nodiscard]] bool HasActiveDownload() const;
  [[nodiscard]] LoginPatchDownloadSnapshot SnapshotActiveDownload() const;
  [[nodiscard]] std::unique_ptr<LoginPatchDownloadSession> TakeActiveDownload();
  void AbortActiveDownload();
  void QueueLoginRestart(std::string account_name, std::string password);
  [[nodiscard]] bool HasQueuedLoginRestart() const;
  [[nodiscard]] std::optional<LoginPatchDownloadRestartRequest>
  TakeQueuedLoginRestart();
  void Clear();

  [[nodiscard]] std::optional<bool> DispatchChunkIfActive(
      std::span<const std::uint8_t> bytes);

 private:
  LoginPatchDownloadBridge() = default;

  mutable std::mutex mutex_;
  std::unique_ptr<LoginPatchDownloadSession> active_download_;
  std::optional<LoginPatchDownloadRestartRequest> queued_login_restart_;
};

bool DispatchActiveLoginInlineDownloadChunk(
    std::span<const std::uint8_t> bytes);

PatchDownloadBootstrapResult BootstrapLoginPatchDownload(
    std::string_view restart_account_name,
    std::string_view restart_password);

}

#include "openwow/net/login_patch_download.h"

#include "openwow/core/console.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/game/battlenet_login.h"
#include "openwow/net/client_services.h"
#include "openwow/net/login_survey_download.h"
#include "openwow/net/os_url_download.h"
#include "openwow/vfs/sfile_core.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace openwow::net {
namespace {

void SecureClearString(std::string& value) {
  if (value.empty()) {
    return;
  }

  volatile char* volatile bytes = value.data();
  for (std::size_t index = 0; index < value.size(); ++index) {
    bytes[index] = '\0';
  }
  value.clear();
}

void SecureClearRestartRequest(
    std::optional<LoginPatchDownloadRestartRequest>& request) {
  if (!request.has_value()) {
    return;
  }

  SecureClearString(request->account_name);
  SecureClearString(request->password);
  request.reset();
}

struct BattlenetManifestDownloadState {
  explicit BattlenetManifestDownloadState(
      std::vector<LoginPatchManifestTransferSnapshot> transfers)
      : manifest(std::move(transfers)) {}

  LoginPatchManifestSession manifest;
  std::atomic<bool> ignore_callbacks{false};
};

LoginPatchManifestTransferSnapshot MakePendingManifestTransferSnapshot(
    const std::filesystem::path& relative_path,
    const std::uint64_t current_extent = 0) {
  LoginPatchManifestTransferSnapshot snapshot;
  snapshot.relative_path = relative_path;
  snapshot.reported_required_extent = -1;
  snapshot.current_extent = current_extent;
  snapshot.ready = false;
  snapshot.transfer_complete = false;
  return snapshot;
}

LoginPatchManifestTransferSnapshot MakeCompletedManifestTransferSnapshot(
    const std::filesystem::path& relative_path,
    std::vector<std::uint8_t> payload) {
  LoginPatchManifestTransferSnapshot snapshot;
  snapshot.relative_path = relative_path;
  snapshot.current_extent = payload.size();
  snapshot.reported_required_extent =
      static_cast<std::int64_t>(snapshot.current_extent);
  snapshot.ready = true;
  snapshot.transfer_complete = true;
  snapshot.payload = std::move(payload);
  return snapshot;
}

LoginPatchManifestTransferSnapshot MakeFailedManifestTransferSnapshot(
    const std::filesystem::path& relative_path) {
  LoginPatchManifestTransferSnapshot snapshot;
  snapshot.relative_path = relative_path;
  snapshot.reported_required_extent = 1;
  snapshot.current_extent = 0;
  snapshot.ready = true;
  snapshot.transfer_complete = false;
  return snapshot;
}

class BattlenetManifestTransferContext {
 public:
  BattlenetManifestTransferContext(
      std::shared_ptr<BattlenetManifestDownloadState> state,
      const std::size_t transfer_index,
      std::filesystem::path relative_path)
      : state_(std::move(state)),
        transfer_index_(transfer_index),
        relative_path_(std::move(relative_path)) {}

  static bool Callback(void* callback_data,
                       const std::uint8_t* bytes,
                       const std::uint32_t byte_count,
                       const std::uint32_t event_flag,
                       const std::uint32_t completion_code) {
    auto* const context =
        static_cast<BattlenetManifestTransferContext*>(callback_data);
    if (context == nullptr) {
      return false;
    }
    return context->HandleCallback(
        bytes, byte_count, event_flag, completion_code);
  }

  void MarkStartFailure() {
    if (!state_->ignore_callbacks.load(std::memory_order_acquire)) {
      state_->manifest.SetTransferSnapshot(
          transfer_index_,
          MakeFailedManifestTransferSnapshot(relative_path_));
    }
    delete this;
  }

 private:
  bool HandleCallback(const std::uint8_t* bytes,
                      const std::uint32_t byte_count,
                      const std::uint32_t event_flag,
                      const std::uint32_t completion_code) {
    if (event_flag == 0u) {
      if (bytes != nullptr && byte_count != 0u) {
        payload_.insert(payload_.end(), bytes, bytes + byte_count);
        if (!state_->ignore_callbacks.load(std::memory_order_acquire)) {
          state_->manifest.SetTransferSnapshot(
              transfer_index_,
              MakePendingManifestTransferSnapshot(relative_path_,
                                                   payload_.size()));
        }
      }
      return true;
    }

    if (!state_->ignore_callbacks.load(std::memory_order_acquire)) {
      if (completion_code ==
          static_cast<std::uint32_t>(OsUrlDownloadCompletionCode::kSuccess)) {
        state_->manifest.SetTransferSnapshot(
            transfer_index_,
            MakeCompletedManifestTransferSnapshot(relative_path_,
                                                   std::move(payload_)));
      } else {
        state_->manifest.SetTransferSnapshot(
            transfer_index_,
            MakeFailedManifestTransferSnapshot(relative_path_));
      }
    }

    delete this;
    return true;
  }

  std::shared_ptr<BattlenetManifestDownloadState> state_;
  std::size_t transfer_index_ = 0;
  std::filesystem::path relative_path_;
  std::vector<std::uint8_t> payload_;
};

class BattlenetManifestPatchSession final : public LoginPatchDownloadSession {
 public:
  explicit BattlenetManifestPatchSession(
      const game::PatchDownloadManifestPlan& plan)
      : state_(std::make_shared<BattlenetManifestDownloadState>(
            BuildInitialTransfers(plan.downloads))) {
    StartTransfers(plan.downloads);
  }

  ~BattlenetManifestPatchSession() override {
    Close(false);
  }

  [[nodiscard]] LoginPatchDownloadState download_state() const override {
    return state_->manifest.download_state();
  }

  [[nodiscard]] LoginPatchDownloadFailureInfo failure_info() const override {
    return state_->manifest.failure_info();
  }

  [[nodiscard]] std::uint64_t current_size() const override {
    return state_->manifest.current_size();
  }

  [[nodiscard]] double progress_ratio() const override {
    return state_->manifest.progress_ratio();
  }

  void AbortTransfer() override {
    state_->ignore_callbacks.store(true, std::memory_order_release);
    state_->manifest.AbortTransfer();
  }

  void Close(const bool delete_file) override {
    (void)delete_file;
    state_->ignore_callbacks.store(true, std::memory_order_release);
  }

  void FinalizeSuccess() override {
    state_->manifest.FinalizeSuccess();
  }

 private:
  static std::vector<LoginPatchManifestTransferSnapshot> BuildInitialTransfers(
      const std::vector<game::PatchDownloadManifestEntry>& downloads) {
    std::vector<LoginPatchManifestTransferSnapshot> transfers;
    transfers.reserve(downloads.size());
    for (const auto& entry : downloads) {
      transfers.push_back(
          MakePendingManifestTransferSnapshot(entry.destination));
    }
    return transfers;
  }

  void StartTransfers(
      const std::vector<game::PatchDownloadManifestEntry>& downloads) {
    for (std::size_t index = 0; index < downloads.size(); ++index) {
      const auto& entry = downloads[index];
      openwow::core::ida::ConsoleLog("Downloading patch %s;%s;%s;%s",
                                     entry.url.c_str(),
                                     entry.destination.c_str(),
                                     entry.detail_2.c_str(),
                                     entry.detail_3.c_str());

      auto* const context = new BattlenetManifestTransferContext(
          state_, index, std::filesystem::path(entry.destination));
      if (!OsURLDownload_Start(entry.url.c_str(),
                               &BattlenetManifestTransferContext::Callback,
                               context,
                               0)) {
        context->MarkStartFailure();
      }
    }
  }

  std::shared_ptr<BattlenetManifestDownloadState> state_;
};

}

LoginPatchDownloadBridge& LoginPatchDownloadBridge::Get() {
  static LoginPatchDownloadBridge bridge;
  return bridge;
}

void LoginPatchDownloadBridge::ReplaceActiveDownload(
    std::unique_ptr<LoginPatchDownloadSession> download) {
  std::lock_guard lock(mutex_);
  active_download_ = std::move(download);
}

bool LoginPatchDownloadBridge::HasActiveDownload() const {
  std::lock_guard lock(mutex_);
  return active_download_ != nullptr;
}

LoginPatchDownloadSnapshot LoginPatchDownloadBridge::SnapshotActiveDownload()
    const {
  std::lock_guard lock(mutex_);

  LoginPatchDownloadSnapshot snapshot{};
  if (active_download_ == nullptr) {
    return snapshot;
  }

  snapshot.has_active_download = true;
  snapshot.state = active_download_->download_state();
  snapshot.result_code = active_download_->failure_info().result_code;
  snapshot.progress_ratio = active_download_->progress_ratio();
  return snapshot;
}

std::unique_ptr<LoginPatchDownloadSession>
LoginPatchDownloadBridge::TakeActiveDownload() {
  std::lock_guard lock(mutex_);
  return std::move(active_download_);
}

void LoginPatchDownloadBridge::AbortActiveDownload() {
  std::unique_ptr<LoginPatchDownloadSession> download = TakeActiveDownload();

  if (download != nullptr) {
    download->AbortTransfer();
  }
}

void LoginPatchDownloadBridge::QueueLoginRestart(std::string account_name,
                                                 std::string password) {
  std::lock_guard lock(mutex_);
  SecureClearRestartRequest(queued_login_restart_);
  queued_login_restart_ = LoginPatchDownloadRestartRequest{
      .account_name = std::move(account_name),
      .password = std::move(password),
  };
}

bool LoginPatchDownloadBridge::HasQueuedLoginRestart() const {
  std::lock_guard lock(mutex_);
  return queued_login_restart_.has_value();
}

std::optional<LoginPatchDownloadRestartRequest>
LoginPatchDownloadBridge::TakeQueuedLoginRestart() {
  std::lock_guard lock(mutex_);
  auto request = std::move(queued_login_restart_);
  SecureClearRestartRequest(queued_login_restart_);
  return request;
}

void LoginPatchDownloadBridge::Clear() {
  std::lock_guard lock(mutex_);
  active_download_.reset();
  SecureClearRestartRequest(queued_login_restart_);
}

std::optional<bool> LoginPatchDownloadBridge::DispatchChunkIfActive(
    const std::span<const std::uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (active_download_ == nullptr) {
    return std::nullopt;
  }
  return active_download_->DispatchInlineChunk(bytes);
}

bool DispatchActiveLoginInlineDownloadChunk(
    const std::span<const std::uint8_t> bytes) {
  if (const auto patch_result =
          LoginPatchDownloadBridge::Get().DispatchChunkIfActive(bytes);
      patch_result.has_value()) {
    return *patch_result;
  }

  if (const auto survey_result =
          LoginSurveyDownloadBridge::Get().DispatchChunkIfActive(bytes);
      survey_result.has_value()) {
    return *survey_result;
  }

  return false;
}

PatchDownloadBootstrapResult BootstrapLoginPatchDownload(
    const std::string_view restart_account_name,
    const std::string_view restart_password) {
  PatchDownloadBootstrapResult result{};

  auto& bridge = LoginPatchDownloadBridge::Get();
  bridge.Clear();

  auto& client_services = ClientServices::Instance();
  if (client_services.GetLoginConnectionType() == LoginConnectionType::kBattleNet) {
    const auto* const battlenet_login = client_services.GetBattlenetLogin();
    if (battlenet_login == nullptr) {
      return result;
    }

    const auto plan = battlenet_login->BuildPatchDownloadPlan();
    if (plan.uses_redirect_monolithic) {
      bridge.QueueLoginRestart(std::string(restart_account_name),
                               std::string(restart_password));
      result.queued_login_restart = true;
      return result;
    }

    bridge.ReplaceActiveDownload(
        std::make_unique<BattlenetManifestPatchSession>(plan));
    result.started_manifest_download = true;
    return result;
  }

  const auto pending_patch = client_services.GetPendingPatchDownloadInfo();
  if (!pending_patch.has_value()) {
    return result;
  }

  std::unique_ptr<LoginInlineDownloadCache> active_download;
  const std::filesystem::path client_root = openwow::vfs::ToNativePath(
      openwow::data::GetCachedStartupWorkingDirectory().c_str());
  const auto bootstrap =
      StartPatchInlineDownload(client_root, pending_patch->filename, pending_patch->digest,
                               pending_patch->expected_size, &active_download);

  if (bootstrap.has_active_download && active_download != nullptr) {
    bridge.ReplaceActiveDownload(std::move(active_download));
    result.started_inline_download = true;
  }

  result.target_path = bootstrap.target_path;
  result.start = bootstrap.start;
  const bool transfer_required =
      bootstrap.start.disposition == LoginInlineDownloadCache::StartDisposition::kTransferRequired;
  client_services.SendLoginFileTransferResponse(
      transfer_required, transfer_required ? bootstrap.start.resume_offset : 0u);
  client_services.ClearPendingPatchDownloadInfo();
  return result;
}

}

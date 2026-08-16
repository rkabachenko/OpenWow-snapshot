#include "openwow/net/login_survey_download.h"

#include "openwow/net/login_inline_download_cache.h"

namespace openwow::net {

LoginSurveyDownloadBridge& LoginSurveyDownloadBridge::Get() {
  static LoginSurveyDownloadBridge bridge;
  return bridge;
}

void LoginSurveyDownloadBridge::ReplaceActiveDownload(
    std::unique_ptr<LoginInlineDownloadCache> download) {
  std::lock_guard lock(mutex_);
  active_download_ = std::move(download);
}

bool LoginSurveyDownloadBridge::HasActiveDownload() const {
  std::lock_guard lock(mutex_);
  return active_download_ != nullptr;
}

void LoginSurveyDownloadBridge::SetCallbackGateFn(LoginSurveyCallbackGateFn fn) {
  std::lock_guard lock(mutex_);
  callback_gate_fn_ = std::move(fn);
}

void LoginSurveyDownloadBridge::SetDisconnectFn(LoginSurveyDisconnectFn fn) {
  std::lock_guard lock(mutex_);
  disconnect_fn_ = std::move(fn);
}

void LoginSurveyDownloadBridge::Clear() {
  std::lock_guard lock(mutex_);
  active_download_.reset();
  callback_gate_fn_ = {};
  disconnect_fn_ = {};
}

std::optional<bool> LoginSurveyDownloadBridge::DispatchChunkIfActive(
    const std::span<const std::uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (active_download_ == nullptr) {
    return std::nullopt;
  }
  return DispatchInlineDownloadChunk(active_download_.get(), bytes);
}

void LoginSurveyDownloadBridge::AbortActiveDownload() {
  std::unique_ptr<LoginInlineDownloadCache> download;
  LoginSurveyCallbackGateFn callback_gate_fn;
  {
    std::lock_guard lock(mutex_);
    download = std::move(active_download_);
    callback_gate_fn = callback_gate_fn_;
  }

  if (download != nullptr) {
    download->AbortTransfer();
  }
  if (callback_gate_fn) {
    callback_gate_fn(false);
  }
}

void LoginSurveyDownloadBridge::AbortAndDisconnect() {
  std::unique_ptr<LoginInlineDownloadCache> download;
  LoginSurveyCallbackGateFn callback_gate_fn;
  LoginSurveyDisconnectFn disconnect_fn;
  {
    std::lock_guard lock(mutex_);
    download = std::move(active_download_);
    callback_gate_fn = callback_gate_fn_;
    disconnect_fn = disconnect_fn_;
  }

  if (download != nullptr) {
    download->AbortTransfer();
  }
  if (callback_gate_fn) {
    callback_gate_fn(false);
  }
  if (disconnect_fn) {
    disconnect_fn();
  }
}

}

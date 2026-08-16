#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

namespace openwow::net {

class LoginInlineDownloadCache;

using LoginSurveyCallbackGateFn = std::function<void(bool)>;
using LoginSurveyDisconnectFn = std::function<void()>;

class LoginSurveyDownloadBridge {
 public:
  static LoginSurveyDownloadBridge& Get();

  void ReplaceActiveDownload(std::unique_ptr<LoginInlineDownloadCache> download);
  [[nodiscard]] bool HasActiveDownload() const;

  void SetCallbackGateFn(LoginSurveyCallbackGateFn fn);
  void SetDisconnectFn(LoginSurveyDisconnectFn fn);

  void Clear();
  [[nodiscard]] std::optional<bool> DispatchChunkIfActive(
      std::span<const std::uint8_t> bytes);
  void AbortActiveDownload();
  void AbortAndDisconnect();

 private:
  LoginSurveyDownloadBridge() = default;

  mutable std::mutex mutex_;
  std::unique_ptr<LoginInlineDownloadCache> active_download_;
  LoginSurveyCallbackGateFn callback_gate_fn_;
  LoginSurveyDisconnectFn disconnect_fn_;
};

}

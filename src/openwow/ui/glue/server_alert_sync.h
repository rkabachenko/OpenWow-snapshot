#pragma once

#include "openwow/net/os_url_download.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace openwow::ui::glue {

class ServerAlertService {
 public:
  using StartDownloadFn =
      std::function<bool(const char*,
                         openwow::net::OsUrlDownloadCallbackFn,
                         void*,
                         int)>;
  using ResolveUrlFn = std::function<std::string()>;
  using DispatchAlertFn = std::function<void(const std::string&)>;

  struct Dependencies {
    StartDownloadFn start_download;
    ResolveUrlFn resolve_url;
  };

  static ServerAlertService& Get();

  bool Start();
  void AbortAndReset();

  [[nodiscard]] bool request_active() const;
  [[nodiscard]] bool has_pending_alert() const;
  [[nodiscard]] bool PumpPendingAlert(const DispatchAlertFn& dispatch_alert);

  static bool DownloadCallback(void* callback_data,
                               const std::uint8_t* bytes,
                               std::uint32_t byte_count,
                               std::uint32_t event_flag,
                               std::uint32_t completion_code);

  void SetDependenciesForTests(Dependencies deps);
  void ResetDependenciesForTests();

 private:
  ServerAlertService();

  [[nodiscard]] static Dependencies MakeDefaultDependencies();
  [[nodiscard]] bool OnDownloadEvent(const std::uint8_t* bytes,
                                     std::uint32_t byte_count,
                                     std::uint32_t event_flag,
                                     std::uint32_t completion_code);

  mutable std::mutex mutex_;
  Dependencies dependencies_;
  std::string response_body_;
  std::string pending_alert_text_;
  bool request_active_ = false;
  bool pending_alert_ = false;
};

}

#include "openwow/ui/glue/server_alert_sync.h"

#include "openwow/game/localization.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace openwow::ui::glue {

namespace {

constexpr int kServerAlertTimeoutMs = 5000;
constexpr std::string_view kServerAlertPrefix = "SERVERALERT:";
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
constexpr std::size_t kServerAlertBufferCapacity = 2047;

void ResetStoredString(std::string& value) {
  std::string empty;
  value.swap(empty);
}

bool StartsWithIgnoreCaseAscii(const std::string_view value,
                               const std::string_view prefix) {
  return value.size() >= prefix.size()
      && openwow::text::EqualsIgnoreCaseAscii(value.substr(0, prefix.size()),
                                              prefix);
}

std::size_t ServerAlertPayloadOffset(const std::string_view response_body) {
  if (StartsWithIgnoreCaseAscii(response_body, kServerAlertPrefix)) {
    return kServerAlertPrefix.size();
  }

  if (response_body.size() < kUtf8Bom.size() + kServerAlertPrefix.size()) {
    return std::string_view::npos;
  }

  if (response_body.substr(0, kUtf8Bom.size()) != kUtf8Bom) {
    return std::string_view::npos;
  }

  const auto prefixed_text = response_body.substr(kUtf8Bom.size());
  if (!StartsWithIgnoreCaseAscii(prefixed_text, kServerAlertPrefix)) {
    return std::string_view::npos;
  }

  return kUtf8Bom.size() + kServerAlertPrefix.size();
}

void AppendTruncatedChunk(std::string& response_body,
                          const std::uint8_t* bytes,
                          const std::uint32_t byte_count) {
  if (bytes == nullptr || byte_count == 0
      || response_body.size() >= kServerAlertBufferCapacity) {
    return;
  }

  const auto remaining_capacity =
      kServerAlertBufferCapacity - response_body.size();
  const auto append_size =
      std::min<std::size_t>(remaining_capacity, byte_count);
  response_body.append(reinterpret_cast<const char*>(bytes), append_size);
}

std::string ResolveServerAlertUrl() {
  const std::string alert_key =
      openwow::ui::game::CVarSystem::Instance().GetCVar("serverAlert");
  if (alert_key.empty()) {
    return {};
  }

  return openwow::game::Localization::Get().GetString(alert_key, alert_key);
}

}

ServerAlertService::ServerAlertService()
    : dependencies_(MakeDefaultDependencies()) {}

ServerAlertService& ServerAlertService::Get() {
  static ServerAlertService instance;
  return instance;
}

ServerAlertService::Dependencies ServerAlertService::MakeDefaultDependencies() {
  return {
      .start_download =
          [](const char* const url,
             openwow::net::OsUrlDownloadCallbackFn callback,
             void* const callback_data,
             const int timeout_ms) {
            return openwow::net::OsURLDownload_Start(
                url, callback, callback_data, timeout_ms);
          },
      .resolve_url = ResolveServerAlertUrl,
  };
}

bool ServerAlertService::Start() {
  Dependencies deps;
  {
    std::lock_guard lock(mutex_);
    ResetStoredString(response_body_);
    ResetStoredString(pending_alert_text_);
    request_active_ = true;
    pending_alert_ = false;
    deps = dependencies_;
  }

  const std::string url =
      deps.resolve_url ? deps.resolve_url() : std::string();
  if (!deps.start_download
      || !deps.start_download(url.c_str(),
                              &ServerAlertService::DownloadCallback,
                              this,
                              kServerAlertTimeoutMs)) {
    std::lock_guard lock(mutex_);
    request_active_ = false;
    ResetStoredString(response_body_);
    ResetStoredString(pending_alert_text_);
    pending_alert_ = false;
    return false;
  }

  return true;
}

void ServerAlertService::AbortAndReset() {
  std::lock_guard lock(mutex_);
  request_active_ = false;
  pending_alert_ = false;
  ResetStoredString(response_body_);
  ResetStoredString(pending_alert_text_);
}

bool ServerAlertService::request_active() const {
  std::lock_guard lock(mutex_);
  return request_active_;
}

bool ServerAlertService::has_pending_alert() const {
  std::lock_guard lock(mutex_);
  return pending_alert_;
}

bool ServerAlertService::PumpPendingAlert(
    const DispatchAlertFn& dispatch_alert) {
  std::string alert_text;
  {
    std::lock_guard lock(mutex_);
    if (!pending_alert_) {
      return false;
    }

    pending_alert_ = false;
    alert_text.swap(pending_alert_text_);
  }

  if (dispatch_alert) {
    dispatch_alert(alert_text);
  }
  return true;
}

bool ServerAlertService::DownloadCallback(
    void* const callback_data,
    const std::uint8_t* const bytes,
    const std::uint32_t byte_count,
    const std::uint32_t event_flag,
    const std::uint32_t completion_code) {
  auto* const service =
      static_cast<ServerAlertService*>(callback_data);
  if (service == nullptr) {
    return true;
  }

  return service->OnDownloadEvent(
      bytes, byte_count, event_flag, completion_code);
}

void ServerAlertService::SetDependenciesForTests(Dependencies deps) {
  std::lock_guard lock(mutex_);
  if (!deps.start_download) {
    deps.start_download = dependencies_.start_download;
  }
  if (!deps.resolve_url) {
    deps.resolve_url = dependencies_.resolve_url;
  }
  dependencies_ = std::move(deps);
  request_active_ = false;
  pending_alert_ = false;
  ResetStoredString(response_body_);
  ResetStoredString(pending_alert_text_);
}

void ServerAlertService::ResetDependenciesForTests() {
  std::lock_guard lock(mutex_);
  dependencies_ = MakeDefaultDependencies();
  request_active_ = false;
  pending_alert_ = false;
  ResetStoredString(response_body_);
  ResetStoredString(pending_alert_text_);
}

bool ServerAlertService::OnDownloadEvent(
    const std::uint8_t* const bytes,
    const std::uint32_t byte_count,
    const std::uint32_t event_flag,
    const std::uint32_t completion_code) {
  (void)completion_code;

  std::lock_guard lock(mutex_);
  if (!request_active_) {
    return true;
  }

  AppendTruncatedChunk(response_body_, bytes, byte_count);
  if (event_flag == 0) {
    return true;
  }

  request_active_ = false;
  pending_alert_ = false;
  ResetStoredString(pending_alert_text_);

  const auto payload_offset = ServerAlertPayloadOffset(response_body_);
  if (payload_offset == std::string_view::npos) {
    return true;
  }

  pending_alert_text_.assign(response_body_.substr(payload_offset));
  pending_alert_ = true;
  return true;
}

}

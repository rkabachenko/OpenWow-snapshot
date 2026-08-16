#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::net {

enum class OsUrlDownloadCompletionCode : std::uint32_t {
  kSuccess = 0,
  kFailure = 1,
  kTimeout = 2,
  kNotFound = 3,
  kNotModified = 4,
  kBodyRejected = 5,
  kForbidden = 6,
  kTransportDomainFailure = 8,
  kCancelled = 9,
  kBadRequest = 10,
};

struct BlizzardHttpRequest {
  std::string url;
  std::uint32_t timeout_ms = 10000;

  bool retrieve_body = true;
  std::optional<std::string> post_body;
  std::optional<std::string> content_type;
  std::optional<std::string> if_modified_since;
  std::optional<std::pair<std::int64_t, std::int64_t>> inclusive_range;
  std::optional<std::pair<std::string, std::string>> cookie;
};

struct BlizzardHttpHeader {
  std::string name;
  std::string value;
};

struct BlizzardHttpResponse {

  int status_code = -1;
  std::vector<BlizzardHttpHeader> headers;
  std::string body;
};

struct BlizzardHttpCallbacks {
  std::function<void(int)> on_status;
  std::function<void()> on_headers_received;
  std::function<void(std::string_view)> on_last_modified;
  std::function<void(std::int64_t)> on_content_length;
  std::function<bool(std::span<const std::uint8_t>)> on_body;

  std::function<bool()> should_cancel;
  std::function<void(OsUrlDownloadCompletionCode)> on_complete;
};

struct BlizzardHttpResult {
  bool success = false;
  OsUrlDownloadCompletionCode completion_code =
      OsUrlDownloadCompletionCode::kFailure;
};

[[nodiscard]] OsUrlDownloadCompletionCode MapBlizzardHttpStatus(
    int status_code) noexcept;

[[nodiscard]] std::optional<std::string> SerializeBlizzardHttpRequest(
    const BlizzardHttpRequest& request);

[[nodiscard]] BlizzardHttpResult ProcessBlizzardHttpResponse(
    const BlizzardHttpResponse& response,
    const BlizzardHttpCallbacks& callbacks = {});

[[nodiscard]] BlizzardHttpResult PerformBlizzardHttpRequest(
    const BlizzardHttpRequest& request,
    const BlizzardHttpCallbacks& callbacks = {});

using OsUrlDownloadCallbackFn =
    bool (*)(void* callback_data,
             const std::uint8_t* bytes,
             std::uint32_t byte_count,
             std::uint32_t event_flag,
             std::uint32_t completion_code);

struct UrlDownloadTestResult {
  bool success = false;
  OsUrlDownloadCompletionCode completion_code =
      OsUrlDownloadCompletionCode::kFailure;
};

class UrlDownloadRangeBuffer {
 public:
  static constexpr std::uint32_t kDefaultTimeoutMs = 10000;

  explicit UrlDownloadRangeBuffer(std::uint32_t expected_byte_count = 0);

  void ResetForDownload(std::uint32_t expected_byte_count = 0);

  void SetInclusiveByteWindowParts(std::uint32_t start_lo,
                                   std::uint32_t start_hi,
                                   std::uint32_t end_lo,
                                   std::uint32_t end_hi);
  [[nodiscard]] std::array<std::uint32_t, 4> GetInclusiveByteWindowParts() const;

  void SetCookiePair(std::string_view cookie_name,
                     std::string_view cookie_value);
  [[nodiscard]] std::pair<std::string_view, std::string_view> GetCookiePair() const;

  void SetTimeoutMs(std::uint32_t timeout_ms) {
    timeout_ms_ = timeout_ms;
  }
  [[nodiscard]] std::uint32_t timeout_ms() const { return timeout_ms_; }

  void SetCompletionCode(std::uint32_t completion_code) {
    completion_code_ = completion_code;
  }
  [[nodiscard]] std::uint32_t completion_code() const { return completion_code_; }

  [[nodiscard]] bool AppendBodyChunk(const std::uint8_t* bytes,
                                     std::uint32_t byte_count);
  [[nodiscard]] static bool Callback(void* callback_data,
                                     const std::uint8_t* bytes,
                                     std::uint32_t byte_count,
                                     std::uint32_t event_flag,
                                     std::uint32_t completion_code);
  void WaitForCompletion();
  [[nodiscard]] bool WaitForCompletionFor(std::chrono::milliseconds timeout);

  [[nodiscard]] const std::string& body() const { return body_; }
  [[nodiscard]] std::uint32_t copied_byte_count() const { return copied_byte_count_; }
  [[nodiscard]] bool reached_window_end() const { return reached_window_end_; }
  [[nodiscard]] bool completed() const { return completed_; }
  [[nodiscard]] bool success() const { return success_; }

 private:
  void MarkCompleted(std::uint32_t completion_code);

  std::string cookie_name_;
  std::string cookie_value_;
  std::string body_;
  std::uint64_t range_start_ = 0;
  std::uint64_t range_end_ = 0;
  std::uint32_t timeout_ms_ = kDefaultTimeoutMs;
  std::uint32_t completion_code_ = 0;
  std::uint32_t copied_byte_count_ = 0;
  bool reached_window_end_ = false;
  bool completed_ = false;
  bool success_ = false;
  mutable std::mutex completion_mutex_;
  std::condition_variable completion_cv_;
};

void OsUrlDownloadCallback(void* hInternet, uintptr_t dwContext,
                            uint32_t dwInternetStatus,
                            void* lpvStatusInformation,
                            uint32_t dwStatusInformationLength);

bool OsURLDownload_Start(const char* url,
                         OsUrlDownloadCallbackFn callback_fn,
                         void* callback_data,
                         int timeout_ms);

bool DownloadUrlToString(const char* url,
                         std::string* body,
                         std::uint32_t timeout_ms);

bool DownloadUrlToStringWithResult(
    const char* url,
    std::string* body,
    std::uint32_t timeout_ms,
    OsUrlDownloadCompletionCode* completion_code);

void SetUrlDownloadHandlerForTests(
    std::function<bool(std::string_view, std::string*)> handler);

void SetUrlDownloadResultHandlerForTests(
    std::function<UrlDownloadTestResult(std::string_view, std::string*)> handler);

void SetUrlDownloadObserverForTests(
    std::function<void(std::string_view, std::uint32_t)> observer);

}


#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/net/os_url_download.h"

namespace openwow::game {

struct AccountMsgHeader {
  std::uint32_t id = 0;
  std::optional<std::string> subject;

  std::int32_t priority = 0;
  bool is_read = false;
};

struct AccountMsgBody {
  std::uint32_t id = 0;
  std::optional<std::string> body_text;
};

enum class AccountMsgState : std::uint32_t {
  kIdle = 0,
  kLoading = 1,
  kLoaded = 2,
  kError = 3,
};

inline constexpr std::array<const char *, 11> kGlueAccountMsgFunctionNames = {
    "AccountMsg_LoadHeaders",
    "AccountMsg_GetNumTotalMsgs",
    "AccountMsg_GetNumUnreadMsgs",
    "AccountMsg_GetNumUnreadUrgentMsgs",
    "AccountMsg_GetIndexHighestPriorityUnreadMsg",
    "AccountMsg_GetIndexNextUnreadMsg",
    "AccountMsg_GetHeaderSubject",
    "AccountMsg_GetHeaderPriority",
    "AccountMsg_LoadBody",
    "AccountMsg_GetBody",
    "AccountMsg_SetMsgRead",
};

class AccountMsg {
public:
  static AccountMsg &Get();

  bool LoadHeaders();

  bool LoadBody(std::uint32_t message_id);

  bool SetMsgRead(std::uint32_t message_index);

  [[nodiscard]] AccountMsgState GetState() const {
    return state_;
  }

  [[nodiscard]] std::uint32_t GetHeaderCount() const {
    return static_cast<std::uint32_t>(headers_.size());
  }

  [[nodiscard]] std::optional<std::uint32_t> ResolveHeaderMessageId(std::uint32_t index) const;

  [[nodiscard]] std::optional<std::string> GetHeaderSubject(std::uint32_t index) const;

  [[nodiscard]] std::int32_t GetHeaderPriority(std::uint32_t index) const;

  [[nodiscard]] std::uint32_t GetNumUnreadMsgs() const;

  [[nodiscard]] std::uint32_t GetNumUnreadUrgentMsgs() const;

  [[nodiscard]] std::int32_t GetIndexHighestPriorityUnreadMsg() const;

  [[nodiscard]] std::int32_t GetIndexNextUnreadMsg(std::int32_t after) const;

  [[nodiscard]] AccountMsgState GetBodyState() const {
    return body_state_;
  }
  [[nodiscard]] const AccountMsgBody &GetBody() const {
    return body_;
  }

  [[nodiscard]] bool Pump();

  void SetGlueFrameEventsRegistered(bool registered);
  void SetHeadersLoadedNotifier(std::function<void()> notifier);
  void SetBodyLoadedNotifier(std::function<void()> notifier);
  void Reset();

private:
  struct RequestContext {
    std::string account_name;
    std::string headers_request_url;
    std::string body_request_url;
    std::string read_request_url;
    std::array<std::uint8_t, 40> session_key{};
  };

  using RequestContextProvider = std::function<std::optional<RequestContext>()>;
  using DownloadStartFn =
      std::function<bool(const char *, openwow::net::OsUrlDownloadCallbackFn, void *, int)>;
  using NotificationFn = std::function<void()>;
  struct QueuedRequestFinalization {
    bool ready = false;
    bool is_error = false;
  };

  AccountMsg();

  [[nodiscard]] static std::optional<RequestContext> BuildDefaultRequestContext();
  [[nodiscard]] static std::string
  BuildSessionKeyHash(const std::array<std::uint8_t, 40> &session_key);
  [[nodiscard]] static bool ParseHeadersResponse(const char *xml_data, std::size_t xml_size,
                                                 std::vector<AccountMsgHeader> *headers,
                                                 std::vector<bool> *read_flags);
  [[nodiscard]] static bool ParseBodyResponse(const char *xml_data, std::size_t xml_size,
                                              std::uint32_t message_id, AccountMsgBody *body);
  [[nodiscard]] static std::string BuildHeadersRequestUrl(const RequestContext &request_context);
  [[nodiscard]] static std::string BuildMessageRequestUrl(const std::string &base_request_url,
                                                          const RequestContext &request_context,
                                                          std::uint32_t message_id);
  [[nodiscard]] static bool IgnoreDownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                                   std::uint32_t byte_count,
                                                   std::uint32_t event_flag,
                                                   std::uint32_t completion_code);
  [[nodiscard]] static bool BodyDownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                                 std::uint32_t byte_count, std::uint32_t event_flag,
                                                 std::uint32_t completion_code);
  [[nodiscard]] static bool HeadersDownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                                    std::uint32_t byte_count,
                                                    std::uint32_t event_flag,
                                                    std::uint32_t completion_code);

  mutable std::mutex mutex_;

  AccountMsgState state_ = AccountMsgState::kIdle;
  std::vector<AccountMsgHeader> headers_;
  std::vector<bool> read_flags_;

  std::string pending_headers_response_;
  std::vector<AccountMsgHeader> queued_headers_;
  std::vector<bool> queued_read_flags_;
  QueuedRequestFinalization headers_finalization_;

  AccountMsgState body_state_ = AccountMsgState::kIdle;
  AccountMsgBody body_;
  std::optional<std::uint32_t> pending_body_message_id_;
  std::string pending_body_response_;
  QueuedRequestFinalization body_finalization_;

  bool headers_pending_ = false;
  bool body_pending_ = false;

  RequestContextProvider request_context_provider_;
  DownloadStartFn download_start_fn_;
  NotificationFn headers_loaded_notifier_;
  NotificationFn body_loaded_notifier_;
  bool glue_frame_events_registered_ = false;
};

}

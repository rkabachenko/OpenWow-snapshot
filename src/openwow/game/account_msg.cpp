
#include "openwow/game/account_msg.h"

#include "openwow/game/localization.h"
#include "openwow/net/auth/auth_session.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/xml/xml_tree.h"

#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <utility>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace openwow::game {

namespace {

constexpr std::array<std::uint8_t, 2> kAccountMsgSessionKeySalt = {0x0B, 'z'};
constexpr int kAccountMsgRequestTimeoutMs = 5000;

constexpr std::int32_t kUrgentAccountMessagePriority = 3;

const char *FindAttributeValue(const openwow::ui::xml::CXMLNode *node,
                               const std::string_view name) {
  if (node == nullptr) {
    return nullptr;
  }

  for (const auto &attribute : node->attributes.entries) {
    if (attribute.name == name) {
      return attribute.value.c_str();
    }
  }

  return nullptr;
}

std::uint32_t ParseXmlUnsignedAttribute(const openwow::ui::xml::CXMLNode *node,
                                        const std::string_view name) {
  const char *const value = FindAttributeValue(node, name);
  if (value == nullptr) {
    return 0;
  }

  return static_cast<std::uint32_t>(std::strtol(value, nullptr, 10));
}

std::int32_t ParseXmlSignedAttribute(const openwow::ui::xml::CXMLNode *node,
                                     const std::string_view name) {
  const char *const value = FindAttributeValue(node, name);
  if (value == nullptr) {
    return 0;
  }

  return static_cast<std::int32_t>(std::atoi(value));
}

}

AccountMsg::AccountMsg()
    : request_context_provider_(&AccountMsg::BuildDefaultRequestContext),
      download_start_fn_([](const char *url, openwow::net::OsUrlDownloadCallbackFn callback_fn,
                            void *callback_data, int timeout_ms) {
        return openwow::net::OsURLDownload_Start(url, callback_fn, callback_data, timeout_ms);
      }) {}

AccountMsg &AccountMsg::Get() {
  static AccountMsg instance;
  return instance;
}

void AccountMsg::SetGlueFrameEventsRegistered(bool registered) {
  std::lock_guard<std::mutex> lock(mutex_);
  glue_frame_events_registered_ = registered;
}

void AccountMsg::SetHeadersLoadedNotifier(std::function<void()> notifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  headers_loaded_notifier_ = std::move(notifier);
}

void AccountMsg::SetBodyLoadedNotifier(std::function<void()> notifier) {
  std::lock_guard<std::mutex> lock(mutex_);
  body_loaded_notifier_ = std::move(notifier);
}

std::optional<std::uint32_t> AccountMsg::ResolveHeaderMessageId(std::uint32_t index) const {
  if (state_ != AccountMsgState::kLoaded || index >= headers_.size()) {
    return std::nullopt;
  }

  return headers_[index].id;
}

std::optional<AccountMsg::RequestContext> AccountMsg::BuildDefaultRequestContext() {
  const auto &account_name = openwow::net::ClientServices::Instance().GetAccountName();
  if (account_name.empty()) {
    return std::nullopt;
  }

  RequestContext context;
  context.account_name = account_name;
  context.headers_request_url = Localization::Get().GetString("ACCOUNT_MESSAGE_HEADERS_URL");
  context.body_request_url = Localization::Get().GetString("ACCOUNT_MESSAGE_BODY_NO_READ_URL");
  context.read_request_url = Localization::Get().GetString("ACCOUNT_MESSAGE_READ_URL");
  context.session_key = openwow::net::auth::AuthSession::Get().GetSessionKey();
  return context;
}

bool AccountMsg::ParseHeadersResponse(const char *xml_data, const std::size_t xml_size,
                                      std::vector<AccountMsgHeader> *const headers,
                                      std::vector<bool> *const read_flags) {
  if (headers == nullptr || read_flags == nullptr) {
    return false;
  }

  headers->clear();
  read_flags->clear();

  auto *const tree = openwow::ui::xml::XMLTree_Parse(xml_data, xml_size);
  if (tree == nullptr || tree->root == nullptr) {
    openwow::ui::xml::XMLTree_Free(tree);
    return false;
  }

  for (auto *header_node = tree->root->first_child; header_node != nullptr;
       header_node = header_node->right_sibling) {
    AccountMsgHeader header;
    header.id = ParseXmlUnsignedAttribute(header_node, "id");
    header.priority = ParseXmlSignedAttribute(header_node, "priority");
    header.is_read = ParseXmlUnsignedAttribute(header_node, "opened") != 0;

    if (const auto *subject_node =
            openwow::ui::xml::XMLNode_FindChildByNameNoCase(header_node, "subject");
        subject_node != nullptr && subject_node->text != nullptr) {
      header.subject.emplace(subject_node->text, subject_node->text_size);
    }

    read_flags->push_back(header.is_read);
    headers->push_back(std::move(header));
  }

  openwow::ui::xml::XMLTree_Free(tree);
  return true;
}

bool AccountMsg::ParseBodyResponse(const char *xml_data, const std::size_t xml_size,
                                   const std::uint32_t message_id, AccountMsgBody *const body) {
  if (body == nullptr) {
    return false;
  }

  auto *const tree = openwow::ui::xml::XMLTree_Parse(xml_data, xml_size);
  if (tree == nullptr || tree->root == nullptr) {
    openwow::ui::xml::XMLTree_Free(tree);
    return false;
  }

  body->id = message_id;
  if (tree->root->text != nullptr) {
    body->body_text.emplace(tree->root->text, tree->root->text_size);
  } else {
    body->body_text.reset();
  }

  openwow::ui::xml::XMLTree_Free(tree);
  return true;
}

std::string AccountMsg::BuildSessionKeyHash(const std::array<std::uint8_t, 40> &session_key) {
  std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
  SHA_CTX sha_context;
  SHA1_Init(&sha_context);
  SHA1_Update(&sha_context, session_key.data(), session_key.size());
  SHA1_Update(&sha_context, kAccountMsgSessionKeySalt.data(), kAccountMsgSessionKeySalt.size());
  SHA1_Final(digest.data(), &sha_context);

  std::ostringstream builder;
  for (const auto byte : digest) {
    builder << static_cast<unsigned int>(byte);
  }
  return builder.str();
}

std::string AccountMsg::BuildHeadersRequestUrl(const RequestContext &request_context) {
  std::string url = request_context.headers_request_url;
  url += "?accountName=";
  url += request_context.account_name;
  url += "&sessionKeyHash=";
  url += BuildSessionKeyHash(request_context.session_key);
  return url;
}

std::string AccountMsg::BuildMessageRequestUrl(const std::string &base_request_url,
                                               const RequestContext &request_context,
                                               const std::uint32_t message_id) {
  std::string url = base_request_url;
  url += "?accountName=";
  url += request_context.account_name;
  url += "&sessionKeyHash=";
  url += BuildSessionKeyHash(request_context.session_key);
  url += "&messageId=";
  url += std::to_string(message_id);
  return url;
}

bool AccountMsg::IgnoreDownloadCallback(void *, const std::uint8_t *, const std::uint32_t,
                                        const std::uint32_t, const std::uint32_t) {
  return false;
}

bool AccountMsg::BodyDownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                      const std::uint32_t byte_count,
                                      const std::uint32_t event_flag,
                                      const std::uint32_t completion_code) {
  auto *const account_msg = static_cast<AccountMsg *>(callback_data);
  if (account_msg == nullptr) {
    return false;
  }

  if (event_flag == 0) {
    std::lock_guard<std::mutex> lock(account_msg->mutex_);
    if (!account_msg->body_pending_ || bytes == nullptr || byte_count == 0) {
      return true;
    }

    account_msg->pending_body_response_.append(reinterpret_cast<const char *>(bytes), byte_count);
    return true;
  }

  std::lock_guard<std::mutex> lock(account_msg->mutex_);
  if (!account_msg->body_pending_) {
    return true;
  }
  account_msg->body_finalization_.ready = true;
  account_msg->body_finalization_.is_error =
      completion_code !=
      static_cast<std::uint32_t>(openwow::net::OsUrlDownloadCompletionCode::kSuccess);
  if (account_msg->body_finalization_.is_error) {
    account_msg->body_state_ = AccountMsgState::kError;
    account_msg->body_ = AccountMsgBody{};
    account_msg->pending_body_message_id_.reset();
    account_msg->pending_body_response_.clear();
    return false;
  }

  const std::uint32_t message_id = account_msg->pending_body_message_id_.value_or(0);
  AccountMsgBody parsed_body;
  if (!ParseBodyResponse(account_msg->pending_body_response_.data(),
                         account_msg->pending_body_response_.size(), message_id, &parsed_body)) {
    account_msg->body_state_ = AccountMsgState::kError;
    account_msg->body_ = AccountMsgBody{};
    account_msg->pending_body_message_id_.reset();
    account_msg->pending_body_response_.clear();
    account_msg->body_finalization_.is_error = true;
    return false;
  }

  account_msg->body_ = std::move(parsed_body);
  account_msg->body_state_ = AccountMsgState::kLoaded;
  account_msg->pending_body_message_id_.reset();
  account_msg->pending_body_response_.clear();
  return true;
}

bool AccountMsg::HeadersDownloadCallback(void *callback_data, const std::uint8_t *bytes,
                                         const std::uint32_t byte_count,
                                         const std::uint32_t event_flag,
                                         const std::uint32_t completion_code) {
  auto *const account_msg = static_cast<AccountMsg *>(callback_data);
  if (account_msg == nullptr) {
    return false;
  }

  if (event_flag == 0) {
    std::lock_guard<std::mutex> lock(account_msg->mutex_);
    if (!account_msg->headers_pending_ || bytes == nullptr || byte_count == 0) {
      return true;
    }

    account_msg->pending_headers_response_.append(reinterpret_cast<const char *>(bytes),
                                                  byte_count);
    return true;
  }

  std::lock_guard<std::mutex> lock(account_msg->mutex_);
  if (!account_msg->headers_pending_) {
    return true;
  }
  account_msg->headers_finalization_.ready = true;
  account_msg->headers_finalization_.is_error =
      completion_code !=
      static_cast<std::uint32_t>(openwow::net::OsUrlDownloadCompletionCode::kSuccess);
  if (account_msg->headers_finalization_.is_error) {
    account_msg->state_ = AccountMsgState::kError;
    account_msg->pending_headers_response_.clear();
    account_msg->queued_headers_.clear();
    account_msg->queued_read_flags_.clear();
    return false;
  }

  std::vector<AccountMsgHeader> parsed_headers;
  std::vector<bool> parsed_read_flags;
  if (!ParseHeadersResponse(account_msg->pending_headers_response_.data(),
                            account_msg->pending_headers_response_.size(), &parsed_headers,
                            &parsed_read_flags)) {
    account_msg->state_ = AccountMsgState::kError;
    account_msg->pending_headers_response_.clear();
    account_msg->queued_headers_.clear();
    account_msg->queued_read_flags_.clear();
    account_msg->headers_finalization_.is_error = true;
    return false;
  }

  account_msg->queued_headers_ = std::move(parsed_headers);
  account_msg->queued_read_flags_ = std::move(parsed_read_flags);
  account_msg->state_ = AccountMsgState::kLoaded;
  account_msg->pending_headers_response_.clear();
  return true;
}

bool AccountMsg::LoadHeaders() {
  std::string request_url;
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_ == AccountMsgState::kLoading || headers_pending_)
    return false;

  const auto request_context =
      request_context_provider_ ? request_context_provider_() : std::optional<RequestContext>{};
  if (!request_context.has_value()) {
    state_ = AccountMsgState::kError;
    headers_finalization_ = {};
    headers_pending_ = false;
    pending_headers_response_.clear();
    queued_headers_.clear();
    queued_read_flags_.clear();
    return false;
  }

  request_url = BuildHeadersRequestUrl(*request_context);
  pending_headers_response_.clear();
  queued_headers_.clear();
  queued_read_flags_.clear();
  headers_finalization_ = {};

  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &AccountMsg::HeadersDownloadCallback, this,
                          kAccountMsgRequestTimeoutMs)) {
    state_ = AccountMsgState::kError;
    headers_finalization_ = {};
    headers_pending_ = false;
    pending_headers_response_.clear();
    queued_headers_.clear();
    queued_read_flags_.clear();
    return false;
  }

  headers_.clear();
  read_flags_.clear();
  state_ = AccountMsgState::kLoading;
  headers_pending_ = true;
  return true;
}

bool AccountMsg::LoadBody(const std::uint32_t message_id) {
  std::string request_url;

  std::lock_guard<std::mutex> lock(mutex_);

  if (body_state_ == AccountMsgState::kLoading || body_pending_) {
    return false;
  }

  const auto request_context =
      request_context_provider_ ? request_context_provider_() : std::optional<RequestContext>{};
  if (!request_context.has_value()) {
    body_state_ = AccountMsgState::kError;
    body_finalization_ = {};
    body_pending_ = false;
    pending_body_message_id_.reset();
    pending_body_response_.clear();
    return false;
  }

  request_url =
      BuildMessageRequestUrl(request_context->body_request_url, *request_context, message_id);
  body_ = AccountMsgBody{};
  pending_body_response_.clear();
  body_finalization_ = {};

  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &AccountMsg::BodyDownloadCallback, this,
                          kAccountMsgRequestTimeoutMs)) {
    body_state_ = AccountMsgState::kError;
    body_finalization_ = {};
    body_pending_ = false;
    pending_body_message_id_.reset();
    pending_body_response_.clear();
    return false;
  }

  body_state_ = AccountMsgState::kLoading;
  body_pending_ = true;
  pending_body_message_id_ = message_id;
  return true;
}

bool AccountMsg::SetMsgRead(std::uint32_t message_index) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (state_ != AccountMsgState::kLoaded)
    return false;
  if (message_index >= headers_.size())
    return false;

  const auto request_context =
      request_context_provider_ ? request_context_provider_() : std::optional<RequestContext>{};
  if (!request_context.has_value()) {
    return false;
  }

  const std::string request_url = BuildMessageRequestUrl(
      request_context->read_request_url, *request_context, headers_[message_index].id);
  if (!download_start_fn_ ||
      !download_start_fn_(request_url.c_str(), &AccountMsg::IgnoreDownloadCallback, nullptr,
                          kAccountMsgRequestTimeoutMs)) {
    return false;
  }

  if (read_flags_.size() < headers_.size())
    read_flags_.resize(headers_.size(), false);
  read_flags_[message_index] = true;
  return true;
}

std::optional<std::string> AccountMsg::GetHeaderSubject(std::uint32_t index) const {
  if (state_ != AccountMsgState::kLoaded)
    return std::nullopt;
  if (index >= headers_.size())
    return std::nullopt;
  return headers_[index].subject;
}

std::int32_t AccountMsg::GetHeaderPriority(std::uint32_t index) const {
  if (state_ == AccountMsgState::kLoading)
    return 0;
  if (index >= headers_.size())
    return 0;
  if (state_ != AccountMsgState::kLoaded)
    return 0;
  return headers_[index].priority;
}

std::uint32_t AccountMsg::GetNumUnreadMsgs() const {
  std::uint32_t count = 0;
  for (std::uint32_t i = 0; i < headers_.size(); ++i) {
    if (i >= read_flags_.size() || !read_flags_[i]) {
      ++count;
    }
  }
  return count;
}

std::uint32_t AccountMsg::GetNumUnreadUrgentMsgs() const {
  std::uint32_t count = 0;
  for (std::uint32_t i = 0; i < headers_.size(); ++i) {
    if (i < read_flags_.size() && read_flags_[i])
      continue;
    if (state_ == AccountMsgState::kLoading)
      continue;
    if (headers_[i].priority >= kUrgentAccountMessagePriority)
      ++count;
  }
  return count;
}

std::int32_t AccountMsg::GetIndexHighestPriorityUnreadMsg() const {
  std::int32_t result = 0;
  if (state_ == AccountMsgState::kLoading)
    return result;

  for (std::uint32_t i = 0; i < headers_.size(); ++i) {
    if ((i >= read_flags_.size() || !read_flags_[i]) &&
        GetHeaderPriority(i) >= kUrgentAccountMessagePriority) {
      result = static_cast<std::int32_t>(i);
    }
  }
  return result;
}

std::int32_t AccountMsg::GetIndexNextUnreadMsg(std::int32_t after) const {
  if (state_ == AccountMsgState::kLoading)
    return -1;
  if (headers_.empty())
    return -1;

  for (std::int32_t i = 0; i < static_cast<std::int32_t>(headers_.size()); ++i) {
    if (i <= after)
      continue;
    if (i >= 0 && i < static_cast<std::int32_t>(headers_.size())) {
      if (i >= static_cast<std::int32_t>(read_flags_.size()) || !read_flags_[i])
        return i;
    }
  }
  return -1;
}

bool AccountMsg::Pump() {
  NotificationFn headers_loaded_notifier;
  NotificationFn body_loaded_notifier;
  bool completed_any_request = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (headers_pending_ && headers_finalization_.ready) {
      completed_any_request = true;
      headers_pending_ = false;
      if (headers_finalization_.is_error) {
        headers_.clear();
        read_flags_.clear();
        queued_headers_.clear();
        queued_read_flags_.clear();
      } else {
        headers_ = std::move(queued_headers_);
        read_flags_ = std::move(queued_read_flags_);
        if (glue_frame_events_registered_) {
          headers_loaded_notifier = headers_loaded_notifier_;
        }
      }
      headers_finalization_ = {};
    }

    if (body_pending_ && body_finalization_.ready) {
      completed_any_request = true;
      body_pending_ = false;
      if (body_finalization_.is_error) {
        body_ = AccountMsgBody{};
      } else if (glue_frame_events_registered_) {
        body_loaded_notifier = body_loaded_notifier_;
      }
      body_finalization_ = {};
    }
  }

  if (headers_loaded_notifier) {
    headers_loaded_notifier();
  }
  if (body_loaded_notifier) {
    body_loaded_notifier();
  }

  return completed_any_request;
}

void AccountMsg::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  state_ = AccountMsgState::kIdle;
  headers_.clear();
  read_flags_.clear();
  pending_headers_response_.clear();
  queued_headers_.clear();
  queued_read_flags_.clear();
  headers_finalization_ = {};
  body_state_ = AccountMsgState::kIdle;
  body_ = AccountMsgBody{};
  pending_body_message_id_.reset();
  pending_body_response_.clear();
  body_finalization_ = {};
  headers_pending_ = false;
  body_pending_ = false;
  request_context_provider_ = &AccountMsg::BuildDefaultRequestContext;
  download_start_fn_ = [](const char *url, openwow::net::OsUrlDownloadCallbackFn callback_fn,
                          void *callback_data, int timeout_ms) {
    return openwow::net::OsURLDownload_Start(url, callback_fn, callback_data, timeout_ms);
  };
  headers_loaded_notifier_ = {};
  body_loaded_notifier_ = {};
  glue_frame_events_registered_ = false;
}

}

#pragma GCC diagnostic pop

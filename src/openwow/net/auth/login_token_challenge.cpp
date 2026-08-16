#include "openwow/net/auth/login_token_challenge.h"

#include <chrono>

namespace openwow::net {
namespace {

constexpr std::size_t kMaxSubmittedTokenChars = 16;
constexpr std::uint32_t kSubmittedTokenStateCode = 14;
constexpr std::uint32_t kSubmittedTokenResultCode = 0;

[[nodiscard]] LoginTokenSubmissionStatus BuildSubmittedTokenStatus() {
  return {
      .state_code = kSubmittedTokenStateCode,
      .result_code = kSubmittedTokenResultCode,
      .state_key = "LOGIN_STATE_CHECKINGVERSIONS",
      .result_key = "LOGIN_OK",
  };
}

}

LoginTokenChallengeBridge& LoginTokenChallengeBridge::Get() {
  static LoginTokenChallengeBridge instance;
  return instance;
}

void LoginTokenChallengeBridge::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = false;
  has_submission_ = false;
  submitted_token_.clear();
  submitted_status_.reset();
  submission_changed_.notify_all();
}

void LoginTokenChallengeBridge::ConfigureChallenge(const bool active) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = active;
  has_submission_ = false;
  submitted_token_.clear();
  submitted_status_.reset();
  if (!active) {
    submission_changed_.notify_all();
  }
}

void LoginTokenChallengeBridge::SubmitToken(const std::string_view token) {
  std::lock_guard<std::mutex> lock(mutex_);
  submitted_token_.assign(token.substr(0, kMaxSubmittedTokenChars));
  has_submission_ = true;
  submitted_status_ = BuildSubmittedTokenStatus();
  submission_changed_.notify_all();
}

bool LoginTokenChallengeBridge::is_active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

bool LoginTokenChallengeBridge::has_submission() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_submission_;
}

std::optional<std::string> LoginTokenChallengeBridge::WaitForSubmission(
    const std::function<bool()>& should_cancel) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!has_submission_) {
    if (!active_ || (should_cancel && should_cancel())) {
      return std::nullopt;
    }
    submission_changed_.wait_for(lock, std::chrono::milliseconds(50));
  }
  return submitted_token_;
}

std::string LoginTokenChallengeBridge::submitted_token() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return submitted_token_;
}

std::optional<LoginTokenSubmissionStatus>
LoginTokenChallengeBridge::submitted_status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return submitted_status_;
}

}

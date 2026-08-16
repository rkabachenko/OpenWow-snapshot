#include "openwow/net/login_survey_result.h"

#include <limits>
#include <utility>

namespace openwow::net {

namespace {

constexpr std::uint8_t kSurveyResultCommand = 4;
constexpr std::uint8_t kSurveyResultSuccess = 0;
constexpr std::uint8_t kSurveyResultError = 1;

}

std::vector<std::uint8_t> BuildLoginSurveyResultPacket(
    const std::uint32_t proof_token,
    const std::span<const std::uint8_t> payload) {
  std::vector<std::uint8_t> packet;
  packet.reserve(1 + sizeof(std::uint32_t) + 1 + sizeof(std::uint16_t) +
                 payload.size());

  packet.push_back(kSurveyResultCommand);
  packet.push_back(static_cast<std::uint8_t>(proof_token & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 8) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 16) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 24) & 0xFFu));
  packet.push_back(kSurveyResultSuccess);

  const auto payload_size = static_cast<std::uint16_t>(payload.size());
  packet.push_back(static_cast<std::uint8_t>(payload_size & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((payload_size >> 8) & 0xFFu));
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

std::vector<std::uint8_t> BuildLoginSurveyResultErrorPacket(
    const std::uint32_t proof_token) {
  std::vector<std::uint8_t> packet;
  packet.reserve(1 + sizeof(std::uint32_t) + 1 + sizeof(std::uint16_t));

  packet.push_back(kSurveyResultCommand);
  packet.push_back(static_cast<std::uint8_t>(proof_token & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 8) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 16) & 0xFFu));
  packet.push_back(static_cast<std::uint8_t>((proof_token >> 24) & 0xFFu));
  packet.push_back(kSurveyResultError);
  packet.push_back(0);
  packet.push_back(0);
  return packet;
}

LoginSurveyResultBridge& LoginSurveyResultBridge::Get() {
  static LoginSurveyResultBridge instance;
  return instance;
}

void LoginSurveyResultBridge::SetSendFn(LoginSurveyResultSendFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  send_fn_ = std::move(fn);
}

void LoginSurveyResultBridge::StagePendingResult(
    const std::uint32_t proof_token,
    std::vector<std::uint8_t> payload) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_ = PendingResult{
      .proof_token = proof_token,
      .payload = std::move(payload),
  };
}

void LoginSurveyResultBridge::ClearPendingResult() {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
}

bool LoginSurveyResultBridge::HasPendingResult() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.has_value() && !pending_->payload.empty();
}

std::uint32_t LoginSurveyResultBridge::pending_proof_token() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.has_value() ? pending_->proof_token : 0;
}

std::vector<std::uint8_t> LoginSurveyResultBridge::pending_payload() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_.has_value() ? pending_->payload : std::vector<std::uint8_t>{};
}

bool LoginSurveyResultBridge::SubmitPendingResult() const {
  LoginSurveyResultSendFn send_fn;
  std::uint32_t proof_token = 0;
  std::vector<std::uint8_t> payload;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pending_.has_value() || pending_->payload.empty() || !send_fn_) {
      return false;
    }
    if (pending_->payload.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
      return false;
    }
    send_fn = send_fn_;
    proof_token = pending_->proof_token;
    payload = pending_->payload;
  }

  return send_fn(BuildLoginSurveyResultPacket(proof_token, payload));
}

}

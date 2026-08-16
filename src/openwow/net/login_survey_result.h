#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace openwow::net {

using LoginSurveyResultSendFn =
    std::function<bool(const std::vector<std::uint8_t>&)>;

[[nodiscard]] std::vector<std::uint8_t> BuildLoginSurveyResultPacket(
    std::uint32_t proof_token,
    std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> BuildLoginSurveyResultErrorPacket(
    std::uint32_t proof_token);

class LoginSurveyResultBridge {
 public:
  static LoginSurveyResultBridge& Get();

  void SetSendFn(LoginSurveyResultSendFn fn);
  void StagePendingResult(std::uint32_t proof_token,
                          std::vector<std::uint8_t> payload);
  void ClearPendingResult();

  [[nodiscard]] bool HasPendingResult() const;
  [[nodiscard]] std::uint32_t pending_proof_token() const;
  [[nodiscard]] std::vector<std::uint8_t> pending_payload() const;

  [[nodiscard]] bool SubmitPendingResult() const;

 private:
  LoginSurveyResultBridge() = default;

  struct PendingResult {
    std::uint32_t proof_token = 0;
    std::vector<std::uint8_t> payload;
  };

  mutable std::mutex mutex_;
  LoginSurveyResultSendFn send_fn_;
  std::optional<PendingResult> pending_;
};

}

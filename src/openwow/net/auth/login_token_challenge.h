#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::net {

struct LoginTokenSubmissionStatus {
  std::uint32_t state_code{0};
  std::uint32_t result_code{0};
  std::string state_key;
  std::string result_key;
};

class LoginTokenChallengeBridge {
 public:
  static LoginTokenChallengeBridge& Get();

  void Reset();
  void ConfigureChallenge(bool active);
  void SubmitToken(std::string_view token);

  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool has_submission() const;
  [[nodiscard]] std::optional<std::string> WaitForSubmission(
      const std::function<bool()>& should_cancel);
  [[nodiscard]] std::string submitted_token() const;
  [[nodiscard]] std::optional<LoginTokenSubmissionStatus> submitted_status() const;

 private:
  LoginTokenChallengeBridge() = default;

  mutable std::mutex mutex_;
  std::condition_variable submission_changed_;
  bool active_{false};
  bool has_submission_{false};
  std::string submitted_token_;
  std::optional<LoginTokenSubmissionStatus> submitted_status_{};
};

}

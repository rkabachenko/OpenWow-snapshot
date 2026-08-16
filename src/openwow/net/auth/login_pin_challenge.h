#pragma once

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>

namespace openwow::net {

using LoginPinChallengeRandomFillFn =
    std::function<void(std::span<std::uint8_t>)>;

struct LoginPinChallengeInfo {
  std::array<std::uint8_t, 10> keypad_shuffle{};
};

struct LoginPinProof {
  std::array<std::uint8_t, 16> client_salt{};
  std::array<std::uint8_t, 20> proof_hash{};
};

class LoginPinChallengeBridge {
 public:
  static LoginPinChallengeBridge& Get();

  void Reset();
  void SetRandomFillFn(LoginPinChallengeRandomFillFn fn);

  void ConfigureChallenge(bool active,
                          std::uint32_t shuffle_seed,
                          std::span<const std::uint8_t, 16> server_salt);
  void SubmitPositions(std::span<const std::uint8_t> positions);

  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool has_submission() const;
  [[nodiscard]] std::optional<LoginPinChallengeInfo> challenge_info() const;
  [[nodiscard]] std::optional<LoginPinProof> WaitForSubmission(
      const std::function<bool()>& should_cancel);
  [[nodiscard]] std::array<std::uint8_t, 10> keypad_shuffle() const;
  [[nodiscard]] std::array<std::uint8_t, 16> server_salt() const;
  [[nodiscard]] std::array<std::uint8_t, 16> client_salt() const;
  [[nodiscard]] std::array<std::uint8_t, 20> proof_hash() const;

 private:
  LoginPinChallengeBridge() = default;

  mutable std::mutex mutex_;
  std::condition_variable submission_changed_;
  LoginPinChallengeRandomFillFn random_fill_fn_;
  bool active_{false};
  bool has_submission_{false};
  std::array<std::uint8_t, 10> keypad_shuffle_{};
  std::array<std::uint8_t, 16> server_salt_{};
  std::array<std::uint8_t, 16> client_salt_{};
  std::array<std::uint8_t, 20> proof_hash_{};
};

}

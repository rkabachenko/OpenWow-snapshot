#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace openwow::net {

struct LoginMatrixCoordinates {
  std::uint32_t column{0};
  std::uint32_t row{0};
};

struct LoginMatrixChallengeInfo {
  std::uint8_t columns{0};
  std::uint8_t rows{0};
  std::uint8_t minimum_digits{0};
  std::uint8_t maximum_digits{0};
  bool flip_coordinates{false};
  std::uint8_t entry_count{0};
};

using LoginMatrixProofKey = std::array<std::uint8_t, 16>;

class LoginMatrixChallengeBridge {
 public:
  static LoginMatrixChallengeBridge& Get();
  ~LoginMatrixChallengeBridge();

  void Reset();

  void ConfigureChallenge(bool active,
                          std::uint32_t column,
                          std::uint32_t row,
                          const LoginMatrixProofKey& proof_key = {});
  void ConfigureChallenge(bool active,
                          std::span<const LoginMatrixCoordinates> coordinates,
                          const LoginMatrixProofKey& proof_key = {});
  [[nodiscard]] bool ConfigureGeneratedChallenge(
      bool active,
      std::uint8_t columns,
      std::uint8_t rows,
      std::uint8_t digits_per_entry,
      std::uint8_t entry_count,
      std::uint64_t selection_seed,
      std::span<const std::uint8_t> session_key);

  void EnterDigit(std::uint8_t digit);
  [[nodiscard]] bool CommitEntry();
  void RevertEntry();

  [[nodiscard]] bool is_active() const;
  [[nodiscard]] bool has_submission() const;
  [[nodiscard]] std::optional<LoginMatrixChallengeInfo> challenge_info() const;
  [[nodiscard]] std::optional<std::array<std::uint8_t, 20>> WaitForSubmission(
      const std::function<bool()>& should_cancel);
  [[nodiscard]] std::optional<LoginMatrixCoordinates> coordinates() const;
  [[nodiscard]] std::array<std::uint8_t, 20> proof_hash() const;

 private:
  LoginMatrixChallengeBridge() = default;

  struct ProofState;

  mutable std::mutex mutex_;
  std::condition_variable submission_changed_;
  bool active_{false};
  bool has_submission_{false};
  std::vector<LoginMatrixCoordinates> coordinates_{};
  std::optional<LoginMatrixChallengeInfo> challenge_info_;
  std::size_t remaining_entries_{0};
  std::unique_ptr<ProofState> proof_state_;
};

}

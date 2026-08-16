#include "openwow/net/auth/login_matrix_challenge.h"

#include "openwow/core/md5.h"
#include "openwow/net/protocol/rc4_cipher.h"
#include "openwow/foundation/hashing/retail_sha1.h"

#include <chrono>

namespace openwow::net {
namespace {

struct MatrixProofWorkingState {
  bool hmac_active{false};
  openwow::foundation::hashing::RetailSha1State inner_ctx{};
  std::array<std::uint8_t, 64> outer_pad{};
  RC4State rc4_state{};
};

LoginMatrixProofKey DeriveProofKey(
    const std::uint64_t selection_seed,
    const std::span<const std::uint8_t> session_key) {
  const std::array<std::uint8_t, 8> seed_bytes = {{
      static_cast<std::uint8_t>(selection_seed),
      static_cast<std::uint8_t>(selection_seed >> 8),
      static_cast<std::uint8_t>(selection_seed >> 16),
      static_cast<std::uint8_t>(selection_seed >> 24),
      static_cast<std::uint8_t>(selection_seed >> 32),
      static_cast<std::uint8_t>(selection_seed >> 40),
      static_cast<std::uint8_t>(selection_seed >> 48),
      static_cast<std::uint8_t>(selection_seed >> 56),
  }};

  openwow::core::MD5Context context{};
  openwow::core::MD5_Init(&context);
  openwow::core::MD5_Update(&context, seed_bytes.data(), seed_bytes.size());
  if (!session_key.empty()) {
    openwow::core::MD5_Update(&context, session_key.data(), session_key.size());
  }
  LoginMatrixProofKey proof_key{};
  openwow::core::MD5_Final(&context, proof_key.data());
  return proof_key;
}

std::vector<LoginMatrixCoordinates> BuildCoordinates(
    const std::uint8_t columns,
    const std::uint8_t rows,
    const std::uint8_t entry_count,
    std::uint64_t selection_seed) {
  const std::size_t total_slots =
      static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
  std::vector<std::uint32_t> pending_slots(total_slots);
  for (std::size_t index = 0; index < pending_slots.size(); ++index) {
    pending_slots[index] = static_cast<std::uint32_t>(index);
  }

  std::vector<LoginMatrixCoordinates> coordinates;
  coordinates.reserve(entry_count);
  for (std::size_t index = 0; index < static_cast<std::size_t>(entry_count); ++index) {
    const std::size_t slot_index =
        static_cast<std::size_t>(selection_seed % pending_slots.size());
    selection_seed /= pending_slots.size();

    const std::uint32_t slot = pending_slots[slot_index];
    coordinates.push_back(LoginMatrixCoordinates{
        .column = slot % columns,
        .row = slot / columns,
    });
    pending_slots.erase(pending_slots.begin() + slot_index);
  }
  return coordinates;
}

MatrixProofWorkingState BuildInitialProofState(
    const LoginMatrixProofKey& proof_key) {
  MatrixProofWorkingState state{};
  state.hmac_active = true;

  std::array<std::uint8_t, 64> inner_pad{};
  inner_pad.fill(0x36u);
  state.outer_pad.fill(0x5Cu);

  for (std::size_t index = 0; index < proof_key.size(); ++index) {
    inner_pad[index] ^= proof_key[index];
    state.outer_pad[index] ^= proof_key[index];
  }

  openwow::foundation::hashing::InitializeRetailSha1(state.inner_ctx);
  openwow::foundation::hashing::UpdateRetailSha1(state.inner_ctx,
      inner_pad.data(),
      static_cast<std::uint32_t>(inner_pad.size()));
  RC4_Init(proof_key.data(),
           static_cast<std::uint32_t>(proof_key.size()),
           state.rc4_state);
  return state;
}

std::array<std::uint8_t, 20> FinalizeCommittedProof(
    MatrixProofWorkingState& state) {
  std::array<std::uint8_t, 20> inner_digest{};
  openwow::foundation::hashing::FinalizeRetailSha1(state.inner_ctx, inner_digest.data());

  openwow::foundation::hashing::RetailSha1State outer_ctx{};
  openwow::foundation::hashing::InitializeRetailSha1(outer_ctx);
  openwow::foundation::hashing::UpdateRetailSha1(outer_ctx,
      state.outer_pad.data(),
      static_cast<std::uint32_t>(state.outer_pad.size()));
  openwow::foundation::hashing::UpdateRetailSha1(outer_ctx,
      inner_digest.data(),
      static_cast<std::uint32_t>(inner_digest.size()));

  std::array<std::uint8_t, 20> proof{};
  openwow::foundation::hashing::FinalizeRetailSha1(outer_ctx, proof.data());
  state.hmac_active = false;
  return proof;
}

}

struct LoginMatrixChallengeBridge::ProofState {
  MatrixProofWorkingState committed{};
  MatrixProofWorkingState working{};
  std::array<std::uint8_t, 20> proof_hash{};
};

LoginMatrixChallengeBridge& LoginMatrixChallengeBridge::Get() {
  static LoginMatrixChallengeBridge instance;
  return instance;
}

LoginMatrixChallengeBridge::~LoginMatrixChallengeBridge() = default;

void LoginMatrixChallengeBridge::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = false;
  has_submission_ = false;
  coordinates_.clear();
  challenge_info_.reset();
  remaining_entries_ = 0;
  proof_state_.reset();
  submission_changed_.notify_all();
}

void LoginMatrixChallengeBridge::ConfigureChallenge(
    const bool active,
    const std::uint32_t column,
    const std::uint32_t row,
    const LoginMatrixProofKey& proof_key) {
  const std::array<LoginMatrixCoordinates, 1> coordinates = {{
      LoginMatrixCoordinates{column, row},
  }};
  ConfigureChallenge(active, coordinates, proof_key);
}

void LoginMatrixChallengeBridge::ConfigureChallenge(
    const bool active,
    const std::span<const LoginMatrixCoordinates> coordinates,
    const LoginMatrixProofKey& proof_key) {
  std::lock_guard<std::mutex> lock(mutex_);

  active_ = active && !coordinates.empty();
  has_submission_ = false;
  coordinates_.assign(coordinates.begin(), coordinates.end());
  challenge_info_.reset();
  remaining_entries_ = active_ ? coordinates_.size() : 0;
  if (!active_) {
    coordinates_.clear();
    remaining_entries_ = 0;
    proof_state_.reset();
    return;
  }

  proof_state_ = std::make_unique<ProofState>();
  proof_state_->committed = BuildInitialProofState(proof_key);
  proof_state_->working = proof_state_->committed;
  proof_state_->proof_hash.fill(0);
}

bool LoginMatrixChallengeBridge::ConfigureGeneratedChallenge(
    const bool active,
    const std::uint8_t columns,
    const std::uint8_t rows,
    const std::uint8_t digits_per_entry,
    const std::uint8_t entry_count,
    const std::uint64_t selection_seed,
    const std::span<const std::uint8_t> session_key) {
  const std::size_t slot_count =
      static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows);
  if (!active || columns == 0 || rows == 0 || digits_per_entry == 0
      || entry_count == 0 || entry_count > slot_count || session_key.empty()) {
    Reset();
    return false;
  }
  const auto coordinates =
      BuildCoordinates(columns, rows, entry_count, selection_seed);
  ConfigureChallenge(active,
                     coordinates,
                     DeriveProofKey(selection_seed, session_key));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    challenge_info_ = LoginMatrixChallengeInfo{
        .columns = columns,
        .rows = rows,
        .minimum_digits = digits_per_entry,
        .maximum_digits = digits_per_entry,
        .flip_coordinates = false,
        .entry_count = entry_count,
    };
  }
  return true;
}

void LoginMatrixChallengeBridge::EnterDigit(const std::uint8_t digit) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || remaining_entries_ == 0 || proof_state_ == nullptr) {
    return;
  }

  std::uint8_t transformed = digit;
  RC4_Process(&transformed, 1u, proof_state_->working.rc4_state);
  openwow::foundation::hashing::UpdateRetailSha1(proof_state_->working.inner_ctx,
                                        &transformed,
                                        1u);
}

bool LoginMatrixChallengeBridge::CommitEntry() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || remaining_entries_ == 0 || proof_state_ == nullptr) {
    return false;
  }

  proof_state_->committed = proof_state_->working;
  --remaining_entries_;
  if (remaining_entries_ != 0) {
    return false;
  }

  proof_state_->proof_hash = FinalizeCommittedProof(proof_state_->committed);
  has_submission_ = true;
  active_ = false;
  coordinates_.clear();
  submission_changed_.notify_all();
  return true;
}

void LoginMatrixChallengeBridge::RevertEntry() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || remaining_entries_ == 0 || proof_state_ == nullptr) {
    return;
  }

  proof_state_->working = proof_state_->committed;
}

bool LoginMatrixChallengeBridge::is_active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

bool LoginMatrixChallengeBridge::has_submission() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_submission_;
}

std::optional<LoginMatrixChallengeInfo>
LoginMatrixChallengeBridge::challenge_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || !challenge_info_.has_value()) {
    return std::nullopt;
  }
  return challenge_info_;
}

std::optional<std::array<std::uint8_t, 20>>
LoginMatrixChallengeBridge::WaitForSubmission(
    const std::function<bool()>& should_cancel) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!has_submission_) {
    if (!active_ || (should_cancel && should_cancel())) {
      return std::nullopt;
    }
    submission_changed_.wait_for(lock, std::chrono::milliseconds(50));
  }
  return proof_state_ != nullptr
      ? std::optional<std::array<std::uint8_t, 20>>(proof_state_->proof_hash)
      : std::nullopt;
}

std::optional<LoginMatrixCoordinates> LoginMatrixChallengeBridge::coordinates() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || remaining_entries_ == 0 || coordinates_.empty()) {
    return std::nullopt;
  }

  const std::size_t index = coordinates_.size() - remaining_entries_;
  if (index >= coordinates_.size()) {
    return std::nullopt;
  }

  return coordinates_[index];
}

std::array<std::uint8_t, 20> LoginMatrixChallengeBridge::proof_hash() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (proof_state_ == nullptr) {
    return {};
  }

  return proof_state_->proof_hash;
}

}

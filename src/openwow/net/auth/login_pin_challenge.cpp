#include "openwow/net/auth/login_pin_challenge.h"

#include "openwow/foundation/hashing/retail_sha1.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <utility>
#include <vector>

#include <openssl/rand.h>

namespace openwow::net {
namespace {

void DefaultRandomFill(const std::span<std::uint8_t> bytes) {
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) == 1) {
    return;
  }

  std::random_device device;
  for (auto& byte : bytes) {
    byte = static_cast<std::uint8_t>(device());
  }
}

std::array<std::uint8_t, 10> BuildKeypadShuffle(std::uint32_t shuffle_seed) {
  std::array<std::uint8_t, 10> pending = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  std::array<std::uint8_t, 10> shuffle{};

  std::uint32_t remaining = 10;
  for (std::uint32_t index = 0; index < shuffle.size(); ++index) {
    const std::uint32_t slot = shuffle_seed % remaining;
    shuffle[index] = pending[slot];
    shuffle_seed /= remaining;
    std::copy(pending.begin() + slot + 1,
              pending.begin() + remaining,
              pending.begin() + slot);
    --remaining;
  }

  return shuffle;
}

std::array<std::uint8_t, 20> BuildPinProofHash(
    const std::array<std::uint8_t, 16>& server_salt,
    const std::array<std::uint8_t, 16>& client_salt,
    const std::span<const std::uint8_t> positions) {
  std::vector<std::uint8_t> position_bytes;
  position_bytes.reserve(positions.size());
  for (const std::uint8_t position : positions) {
    position_bytes.push_back(static_cast<std::uint8_t>(position + 48u));
  }

  openwow::foundation::hashing::RetailSha1State inner{};
  openwow::foundation::hashing::InitializeRetailSha1(inner);
  openwow::foundation::hashing::UpdateRetailSha1(inner, server_salt.data(),
                                        static_cast<std::uint32_t>(server_salt.size()));
  if (!position_bytes.empty()) {
    openwow::foundation::hashing::UpdateRetailSha1(inner, position_bytes.data(),
                                          static_cast<std::uint32_t>(position_bytes.size()));
  }

  std::array<std::uint8_t, 20> inner_digest{};
  openwow::foundation::hashing::FinalizeRetailSha1(inner, inner_digest.data());

  openwow::foundation::hashing::RetailSha1State outer{};
  openwow::foundation::hashing::InitializeRetailSha1(outer);
  openwow::foundation::hashing::UpdateRetailSha1(outer, client_salt.data(),
                                        static_cast<std::uint32_t>(client_salt.size()));
  openwow::foundation::hashing::UpdateRetailSha1(outer, inner_digest.data(),
                                        static_cast<std::uint32_t>(inner_digest.size()));

  std::array<std::uint8_t, 20> proof_hash{};
  openwow::foundation::hashing::FinalizeRetailSha1(outer, proof_hash.data());
  return proof_hash;
}

}

LoginPinChallengeBridge& LoginPinChallengeBridge::Get() {
  static LoginPinChallengeBridge instance;
  return instance;
}

void LoginPinChallengeBridge::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  random_fill_fn_ = {};
  active_ = false;
  has_submission_ = false;
  keypad_shuffle_.fill(0);
  server_salt_.fill(0);
  client_salt_.fill(0);
  proof_hash_.fill(0);
  submission_changed_.notify_all();
}

void LoginPinChallengeBridge::SetRandomFillFn(
    LoginPinChallengeRandomFillFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  random_fill_fn_ = std::move(fn);
}

void LoginPinChallengeBridge::ConfigureChallenge(
    const bool active,
    const std::uint32_t shuffle_seed,
    const std::span<const std::uint8_t, 16> server_salt) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = active;
  has_submission_ = false;
  client_salt_.fill(0);
  proof_hash_.fill(0);
  if (!active) {
    keypad_shuffle_.fill(0);
    server_salt_.fill(0);
    submission_changed_.notify_all();
    return;
  }

  keypad_shuffle_ = BuildKeypadShuffle(shuffle_seed);
  std::copy(server_salt.begin(), server_salt.end(), server_salt_.begin());
}

void LoginPinChallengeBridge::SubmitPositions(
    const std::span<const std::uint8_t> positions) {
  LoginPinChallengeRandomFillFn random_fill_fn;
  std::array<std::uint8_t, 16> server_salt{};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    random_fill_fn = random_fill_fn_;
    server_salt = server_salt_;
  }

  std::array<std::uint8_t, 16> client_salt{};
  if (random_fill_fn) {
    random_fill_fn(client_salt);
  } else {
    DefaultRandomFill(client_salt);
  }

  const auto proof_hash = BuildPinProofHash(server_salt, client_salt, positions);

  std::lock_guard<std::mutex> lock(mutex_);
  client_salt_ = client_salt;
  proof_hash_ = proof_hash;
  has_submission_ = true;
  submission_changed_.notify_all();
}

bool LoginPinChallengeBridge::is_active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

bool LoginPinChallengeBridge::has_submission() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_submission_;
}

std::optional<LoginPinChallengeInfo>
LoginPinChallengeBridge::challenge_info() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || has_submission_) {
    return std::nullopt;
  }
  return LoginPinChallengeInfo{.keypad_shuffle = keypad_shuffle_};
}

std::optional<LoginPinProof> LoginPinChallengeBridge::WaitForSubmission(
    const std::function<bool()>& should_cancel) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (!has_submission_) {
    if (!active_ || (should_cancel && should_cancel())) {
      return std::nullopt;
    }
    submission_changed_.wait_for(lock, std::chrono::milliseconds(50));
  }
  return LoginPinProof{
      .client_salt = client_salt_,
      .proof_hash = proof_hash_,
  };
}

std::array<std::uint8_t, 10> LoginPinChallengeBridge::keypad_shuffle() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return keypad_shuffle_;
}

std::array<std::uint8_t, 16> LoginPinChallengeBridge::server_salt() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return server_salt_;
}

std::array<std::uint8_t, 16> LoginPinChallengeBridge::client_salt() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return client_salt_;
}

std::array<std::uint8_t, 20> LoginPinChallengeBridge::proof_hash() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return proof_hash_;
}

}

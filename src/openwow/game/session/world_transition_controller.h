#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "openwow/game/session/session_observations.h"

namespace openwow::game {

enum class WorldState : std::uint8_t {
  kDisconnected = 0,
  kLoggingIn,
  kLoadingScreen,
  kInWorld,
  kTeleporting,
};

struct WorldTransferRequest {
  std::uint32_t map_id{0};
  float x{0};
  float y{0};
  float z{0};
  float orientation{0};
  std::string map_internal_name;
  bool send_worldport_ack{false};
  WorldState state_after_dispatch{WorldState::kTeleporting};

  bool is_transport_relative{false};
};

class WorldTransitionController final {
 public:
  void Stage(WorldTransferRequest request) {
    staged_ = std::move(request);
  }
  void CancelStaged() { staged_.reset(); }
  [[nodiscard]] bool HasStaged() const noexcept { return staged_.has_value(); }
  [[nodiscard]] std::optional<WorldTransferRequest> TakeStaged() {
    return std::exchange(staged_, std::nullopt);
  }
  [[nodiscard]] TransferAbortedInfo& transfer_aborted() noexcept {
    return transfer_aborted_;
  }
  [[nodiscard]] const TransferAbortedInfo& transfer_aborted() const noexcept {
    return transfer_aborted_;
  }
  [[nodiscard]] QueryTimeResponse& query_time() noexcept { return query_time_; }
  [[nodiscard]] const QueryTimeResponse& query_time() const noexcept {
    return query_time_;
  }

 private:
  std::optional<WorldTransferRequest> staged_;
  TransferAbortedInfo transfer_aborted_;
  QueryTimeResponse query_time_;
};

}

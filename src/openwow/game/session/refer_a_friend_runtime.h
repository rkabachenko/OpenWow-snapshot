#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace openwow::game {

class ReferAFriendRuntime final {
 public:
  struct PendingFailure {
    std::uint64_t target_guid{0};
    std::uint32_t reason{0};
  };

  [[nodiscard]] std::vector<PendingFailure>& pending_failures() noexcept {
    return pending_failures_;
  }
  [[nodiscard]] const std::vector<PendingFailure>& pending_failures() const noexcept {
    return pending_failures_;
  }
  void Reset() {
    pending_failures_.clear();
    ClearLevelGrant();
  }
  [[nodiscard]] std::uint64_t pending_level_grant_guid() const noexcept {
    return pending_level_grant_guid_;
  }
  void BeginLevelGrant(std::uint64_t guid) noexcept {
    pending_level_grant_guid_ = guid;
    level_grant_event_dispatched_ = false;
  }
  void ClearLevelGrant() noexcept {
    pending_level_grant_guid_ = 0;
    level_grant_event_dispatched_ = false;
  }
  [[nodiscard]] bool level_grant_event_dispatched() const noexcept {
    return level_grant_event_dispatched_;
  }
  void MarkLevelGrantEventDispatched() noexcept {
    level_grant_event_dispatched_ = true;
  }

 private:
  std::vector<PendingFailure> pending_failures_;
  std::uint64_t pending_level_grant_guid_{0};
  bool level_grant_event_dispatched_{false};
};

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace openwow::game {

struct PendingSummonInfo {
  std::uint64_t summoner_guid{0};
  std::uint32_t zone_id{0};
  std::uint32_t timeout_ms{0};
  std::uint32_t expiration_time_ms{0};
};

class SummonInteraction final {
 public:
  [[nodiscard]] bool ApplyRequest(const std::uint8_t* data, std::size_t size,
                                  std::uint32_t current_time_ms);
  void Clear() noexcept { pending_ = {}; }
  [[nodiscard]] const PendingSummonInfo& pending() const noexcept {
    return pending_;
  }
  [[nodiscard]] std::uint32_t SecondsRemaining(
      std::uint32_t current_time_ms) const noexcept;

 private:
  PendingSummonInfo pending_{};
};

struct RaidTargetIcon {
  std::uint8_t icon_id{0};
  std::uint64_t target_guid{0};
};

struct RaidTargetUpdate {
  bool replace_all{false};
  std::vector<RaidTargetIcon> icons;
};

struct RaidReadyCheck {
  std::uint64_t initiator_guid{0};
};

struct RaidReadyCheckConfirm {
  std::uint64_t player_guid{0};
  bool ready{false};
};

struct PartyAssignment {
  std::uint8_t role{0};
  bool apply{false};
  std::uint64_t target_guid{0};
};

[[nodiscard]] std::optional<RaidTargetUpdate> DecodeRaidTargetUpdate(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<RaidReadyCheck> DecodeRaidReadyCheck(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] std::optional<RaidReadyCheckConfirm> DecodeRaidReadyCheckConfirm(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] bool DecodeRaidReadyCheckFinished(const std::uint8_t* data,
                                                std::size_t size);
[[nodiscard]] std::optional<PartyAssignment> DecodePartyAssignment(
    const std::uint8_t* data, std::size_t size);

}

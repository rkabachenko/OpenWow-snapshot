#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class WsgFlagState : std::uint8_t {
  kInBase   = 1,
  kCarried  = 2,
  kOnGround = 3,
  kUnknown  = 0,
};

class BgWarsongGulch {
 public:
  static constexpr std::int32_t kWsAllianceScore       = 1545;
  static constexpr std::int32_t kWsHordeScore           = 1546;
  static constexpr std::int32_t kWsHordeFlagState       = 1581;
  static constexpr std::int32_t kWsAllianceFlagState    = 1582;
  static constexpr std::int32_t kWsHordeFlagCarrier     = 2338;
  static constexpr std::int32_t kWsAllianceFlagCarrier  = 2339;
  static constexpr std::int32_t kWsTimeRemaining        = 4248;

  static constexpr int kMaxCaptures        = 3;
  static constexpr float kMatchDuration    = 25.0f * 60.0f;
  static constexpr float kDropReturnTime   = 10.0f;

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);

  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] WsgFlagState GetAllianceFlagState() const { return alliance_flag_state_; }
  [[nodiscard]] WsgFlagState GetHordeFlagState() const { return horde_flag_state_; }

  [[nodiscard]] std::uint32_t GetAllianceFlagCarrier() const { return alliance_flag_carrier_; }
  [[nodiscard]] std::uint32_t GetHordeFlagCarrier() const { return horde_flag_carrier_; }

  [[nodiscard]] int GetAllianceScore() const { return alliance_score_; }
  [[nodiscard]] int GetHordeScore() const { return horde_score_; }

  [[nodiscard]] float GetRemainingTime() const { return remaining_time_; }

  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] std::string GetScoreText() const;

  [[nodiscard]] static std::string GetFlagStateText(WsgFlagState state);

  [[nodiscard]] std::string GetFormattedTimeRemaining() const;

  [[nodiscard]] int GetWinner() const;

  [[nodiscard]] std::string GetStatusText() const;

  [[nodiscard]] bool AreBothFlagsInBase() const;
  void Reset();

 private:
  WsgFlagState alliance_flag_state_{WsgFlagState::kInBase};
  WsgFlagState horde_flag_state_{WsgFlagState::kInBase};
  std::uint32_t alliance_flag_carrier_{0};
  std::uint32_t horde_flag_carrier_{0};
  int alliance_score_{0};
  int horde_score_{0};
  float remaining_time_{kMatchDuration};
};

}

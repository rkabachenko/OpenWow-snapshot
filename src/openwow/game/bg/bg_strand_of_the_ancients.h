#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

enum class SotaGateState : std::uint8_t {
  kOk        = 1,
  kDamaged   = 2,
  kDestroyed = 3,
  kUnknown   = 0,
};

enum class SotaGraveyardControl : std::uint8_t {
  kNeutral   = 0,
  kAlliance  = 1,
  kHorde     = 2,
};

enum class SotaGate : std::uint8_t {
  kGreen     = 0,
  kBlue      = 1,
  kRed       = 2,
  kPurple    = 3,
  kYellow    = 4,
  kAncient   = 5,
  kCount     = 6,
};

enum class SotaGraveyard : std::uint8_t {
  kLeft      = 0,
  kCenter    = 1,
  kRight     = 2,
  kCount     = 3,
};

class BgStrandOfTheAncients {
 public:

  static constexpr std::int32_t kWsAllianceAttacks     = 4352;
  static constexpr std::int32_t kWsHordeAttacks        = 4353;

  static constexpr std::int32_t kWsPurpleGate          = 3614;
  static constexpr std::int32_t kWsRedGate             = 3617;
  static constexpr std::int32_t kWsBlueGate            = 3620;
  static constexpr std::int32_t kWsGreenGate           = 3623;
  static constexpr std::int32_t kWsYellowGate          = 3638;
  static constexpr std::int32_t kWsAncientGate         = 3849;

  static constexpr std::int32_t kWsLeftGYAlliance      = 3635;
  static constexpr std::int32_t kWsCenterGYAlliance    = 3637;
  static constexpr std::int32_t kWsRightGYAlliance     = 3636;
  static constexpr std::int32_t kWsLeftGYHorde         = 3632;
  static constexpr std::int32_t kWsCenterGYHorde       = 3634;
  static constexpr std::int32_t kWsRightGYHorde        = 3633;

  static constexpr std::int32_t kWsTimerMins           = 3559;
  static constexpr std::int32_t kWsTimerSecTens        = 3560;
  static constexpr std::int32_t kWsTimerSecDecs        = 3561;
  static constexpr std::int32_t kWsBonusTimer          = 3571;
  static constexpr std::int32_t kWsEnableTimer         = 3564;

  static constexpr std::int32_t kWsRightAttTokenAll    = 3627;
  static constexpr std::int32_t kWsLeftAttTokenAll     = 3626;
  static constexpr std::int32_t kWsLeftAttTokenHorde   = 3629;
  static constexpr std::int32_t kWsRightAttTokenHorde  = 3628;
  static constexpr std::int32_t kWsAllianceDefToken    = 3630;
  static constexpr std::int32_t kWsHordeDefToken       = 3631;

  static constexpr std::int32_t kGateWS[6] = {
    kWsGreenGate,
    kWsBlueGate,
    kWsRedGate,
    kWsPurpleGate,
    kWsYellowGate,
    kWsAncientGate,
  };

  static constexpr int kMaxRounds           = 2;
  static constexpr float kRoundDuration     = 600.0f;

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);

  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] SotaGateState GetGateState(SotaGate gate) const;
  [[nodiscard]] int GetGateStateValue(SotaGate gate) const;

  [[nodiscard]] SotaGraveyardControl GetGraveyardControl(SotaGraveyard gy) const;

  [[nodiscard]] bool IsAllianceAttacking() const { return alliance_attacking_; }
  [[nodiscard]] bool IsHordeAttacking() const { return horde_attacking_; }

  [[nodiscard]] int GetAttackingFaction() const;

  [[nodiscard]] bool IsTimerEnabled() const { return timer_enabled_; }
  [[nodiscard]] bool IsBonusRound() const { return bonus_round_; }

  [[nodiscard]] int GetTimerMins() const { return timer_mins_; }
  [[nodiscard]] int GetTimerSecTens() const { return timer_sec_tens_; }
  [[nodiscard]] int GetTimerSecDecs() const { return timer_sec_decs_; }

  [[nodiscard]] int GetTotalRoundTimeRemainingSec() const;
  [[nodiscard]] std::string GetFormattedTimeRemaining() const;

  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] std::string GetStatusText() const;

  [[nodiscard]] static std::string_view GetGateName(SotaGate gate);
  [[nodiscard]] static std::string_view GetGraveyardName(SotaGraveyard gy);
  [[nodiscard]] static std::string_view GetGateStateName(SotaGateState state);

  void Reset();

 private:
  bool alliance_attacking_{false};
  bool horde_attacking_{false};

  std::array<SotaGateState, static_cast<std::size_t>(SotaGate::kCount)> gates_{};
  std::array<SotaGraveyardControl, static_cast<std::size_t>(SotaGraveyard::kCount)> graveyards_{};

  bool timer_enabled_{false};
  bool bonus_round_{false};

  int timer_mins_{0};
  int timer_sec_tens_{0};
  int timer_sec_decs_{0};
};

}

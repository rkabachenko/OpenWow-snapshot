#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace openwow::game {

enum class EotsTowerState : std::uint8_t {
  kNeutral    = 0,
  kAlliance   = 1,
  kHorde      = 2,
  kUnknown    = 3,
};

enum class EotsTower : std::uint8_t {
  kFelReaver     = 0,
  kBloodElf      = 1,
  kDraeneiRuins  = 2,
  kMageTower     = 3,
  kCount         = 4,
};

enum class EotsFlagCarrier : std::uint8_t {
  kNone          = 0,
  kAlliance      = 1,
  kHorde         = 2,
};

class BgEyeOfTheStorm {
 public:

  static constexpr std::int32_t kWsAllianceScore       = 2749;
  static constexpr std::int32_t kWsHordeScore           = 2750;
  static constexpr std::int32_t kWsAllianceBaseCount    = 2752;
  static constexpr std::int32_t kWsHordeBaseCount       = 2753;
  static constexpr std::int32_t kWsFlagAtCenter         = 2757;
  static constexpr std::int32_t kWsFlagStateAlliance    = 2769;
  static constexpr std::int32_t kWsFlagStateHorde       = 2770;
  static constexpr std::int32_t kWsProgressBarShow      = 2718;
  static constexpr std::int32_t kWsProgressBarStatus    = 2719;
  static constexpr std::int32_t kWsProgressBarGrey      = 2720;

  struct TowerIconWorldStates {
    std::int32_t neutral;
    std::int32_t alliance_controlled;
    std::int32_t horde_controlled;
  };

  static constexpr TowerIconWorldStates kTowerIconWS[4] = {
    {2725, 2726, 2727},
    {2722, 2723, 2724},
    {2731, 2732, 2733},
    {2728, 2730, 2729},
  };

  struct TowerConflictWorldStates {
    std::int32_t alliance_conflict;
    std::int32_t horde_conflict;
  };

  static constexpr TowerConflictWorldStates kTowerConflictWS[4] = {
    {2739, 2740},
    {2735, 2736},
    {2738, 2737},
    {2741, 2742},
  };

  static constexpr int kMaxScore              = 1600;
  static constexpr float kMatchDuration       = 25.0f * 60.0f;

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);

  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] int GetAllianceScore() const { return alliance_score_; }
  [[nodiscard]] int GetHordeScore() const { return horde_score_; }

  [[nodiscard]] int GetAllianceBaseCount() const { return alliance_base_count_; }
  [[nodiscard]] int GetHordeBaseCount() const { return horde_base_count_; }

  [[nodiscard]] EotsTowerState GetTowerState(EotsTower tower) const;

  [[nodiscard]] int GetTowersControlledByAlliance() const;
  [[nodiscard]] int GetTowersControlledByHorde() const;

  [[nodiscard]] EotsFlagCarrier GetFlagCarrier() const;
  [[nodiscard]] bool IsFlagAtCenter() const { return flag_at_center_; }

  [[nodiscard]] int GetProgressBarStatus() const { return progress_bar_status_; }
  [[nodiscard]] bool IsProgressBarShown() const { return progress_bar_shown_; }

  [[nodiscard]] float GetRemainingTime() const { return remaining_time_; }
  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] std::string GetScoreText() const;
  [[nodiscard]] std::string GetFormattedTimeRemaining() const;
  [[nodiscard]] std::string GetStatusText() const;

  [[nodiscard]] int GetWinner() const;

  [[nodiscard]] static std::string_view GetTowerName(EotsTower tower);
  [[nodiscard]] static std::string GetFlagCarrierText(EotsFlagCarrier carrier);

  void Reset();

 private:
  int alliance_score_{0};
  int horde_score_{0};
  int alliance_base_count_{0};
  int horde_base_count_{0};

  std::array<EotsTowerState, static_cast<std::size_t>(EotsTower::kCount)> towers_{};

  bool flag_at_center_{true};
  std::uint8_t flag_state_alliance_{1};
  std::uint8_t flag_state_horde_{1};

  int progress_bar_status_{50};
  bool progress_bar_shown_{false};

  float remaining_time_{kMatchDuration};
};

}

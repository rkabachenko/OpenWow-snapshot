#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace openwow::game {

enum class AbBaseState : std::uint8_t {
  kNeutral            = 0,
  kAllianceControlled = 1,
  kHordeControlled    = 2,
  kAllianceAssaulting = 3,
  kHordeAssaulting    = 4,
};

enum class AbBase : std::uint8_t {
  kStables    = 0,
  kBlacksmith = 1,
  kFarm       = 2,
  kLumberMill = 3,
  kGoldMine   = 4,
  kCount      = 5,
};

class BgArathiBasin {
 public:

  static constexpr std::int32_t kWsAllianceResources = 1776;
  static constexpr std::int32_t kWsHordeResources    = 1777;

  struct BaseWorldStates {
    std::int32_t alliance_controlled;
    std::int32_t horde_controlled;
    std::int32_t alliance_assaulting;
    std::int32_t horde_assaulting;
  };

  static constexpr BaseWorldStates kBaseWS[5] = {

    {1767, 1768, 1769, 1770},

    {1772, 1773, 1774, 1775},

    {1777, 1778, 1779, 1780},

    {1782, 1783, 1784, 1785},

    {1787, 1788, 1789, 1790},
  };

  static constexpr int kMaxResources = 1600;
  static constexpr float kMatchDuration = 25.0f * 60.0f;

  static constexpr int kResourcesPerTick[6] = {0, 10, 10, 10, 30, 30};

  [[nodiscard]] static int GetResourceTickRate(int base_count);

  [[nodiscard]] static bool IsRelevantWorldState(std::int32_t ws_id);
  void OnWorldStateUpdate(std::int32_t ws_id, std::int32_t value);

  void Update(float dt);

  [[nodiscard]] AbBaseState GetBaseState(AbBase base) const;
  [[nodiscard]] int GetAllianceResources() const { return alliance_resources_; }
  [[nodiscard]] int GetHordeResources() const { return horde_resources_; }
  [[nodiscard]] int GetAllianceBaseCount() const;
  [[nodiscard]] int GetHordeBaseCount() const;
  [[nodiscard]] float GetRemainingTime() const { return remaining_time_; }
  [[nodiscard]] bool IsFinished() const;

  [[nodiscard]] static std::string_view GetBaseName(AbBase base);

  void Reset();

 private:
  std::array<AbBaseState, static_cast<std::size_t>(AbBase::kCount)> bases_{};
  int alliance_resources_{0};
  int horde_resources_{0};
  float remaining_time_{kMatchDuration};
};

}

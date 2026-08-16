#pragma once

#include <cstdint>

namespace openwow::game {

enum class RestState : std::uint8_t {
  Normal,
  Rested,
  Exhausted,
};

class RestStateManager {
 public:

  void SetInRestArea(bool in_rest_area);
  [[nodiscard]] bool IsInRestArea() const;

  [[nodiscard]] RestState GetState() const;

  void SetRestedXP(std::uint32_t xp);
  [[nodiscard]] std::uint32_t GetRestedXP() const;

  [[nodiscard]] float GetRestedPercent(std::uint32_t next_level_xp) const;

  [[nodiscard]] std::uint32_t GetMaxRestedXP(std::uint32_t next_level_xp) const;

  [[nodiscard]] bool IsFullyRested(std::uint32_t next_level_xp) const;

  void AccumulateRest(float dt, std::uint32_t next_level_xp);

  std::uint32_t ConsumeRestXP(std::uint32_t xp_gain);

  [[nodiscard]] float GetRestXPRate() const;

  [[nodiscard]] float GetTimeToFullRest(std::uint32_t next_level_xp) const;

  [[nodiscard]] bool GetRestIcon() const;

  void SetLogoutLocation(bool is_inn);

  void Reset();

 private:
  bool in_rest_area_{false};
  bool logged_out_in_inn_{false};
  std::uint32_t rested_xp_{0};

  std::uint32_t cached_next_level_xp_{0};
};

}

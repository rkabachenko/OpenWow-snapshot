
#pragma once

#include <cstdint>

namespace openwow::game {

enum class PvPFlagState : std::uint8_t {
  Off = 0,
  PvP = 1,
  FFA = 2,
};

class PvPFlagManager {
 public:

  void SetFlagged(bool flagged);
  [[nodiscard]] bool IsFlagged() const { return flagged_; }

  void SetFFA(bool ffa);
  [[nodiscard]] bool IsFFA() const { return ffa_; }

  [[nodiscard]] PvPFlagState GetState() const;

  void TogglePvP();

  void SetAutoFlagTimer(float seconds) { auto_flag_timer_ = seconds; }
  [[nodiscard]] float GetAutoFlagTimer() const { return auto_flag_timer_; }
  [[nodiscard]] bool IsTimerActive() const { return auto_flag_timer_ > 0.0f; }

  void SetInPvPZone(bool v) { in_pvp_zone_ = v; }
  [[nodiscard]] bool IsInPvPZone() const { return in_pvp_zone_; }

  void SetInSanctuary(bool v) { in_sanctuary_ = v; }
  [[nodiscard]] bool IsInSanctuary() const { return in_sanctuary_; }

  [[nodiscard]] bool CanAttackPlayer() const {
    return (flagged_ || ffa_) && !in_sanctuary_;
  }

  [[nodiscard]] bool CanBeAttacked() const { return flagged_ || ffa_; }

  [[nodiscard]] std::uint32_t GetFlagColor() const;

  void Update(float dt);

  void Reset();

 private:
  static constexpr float kDefaultAutoTimer = 300.0f;

  bool flagged_{false};
  bool ffa_{false};
  bool in_pvp_zone_{false};
  bool in_sanctuary_{false};
  float auto_flag_timer_{0.0f};
};

}

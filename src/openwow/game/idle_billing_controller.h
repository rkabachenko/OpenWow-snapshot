#pragma once

#include <cstdint>

namespace openwow::game {

struct WorldBillingState {
  std::uint32_t time_remaining_minutes{0};
  std::uint8_t flags{0};
};

struct IdleBillingUpdateContext {
  std::uint32_t now_ms{0};
  bool has_active_player{false};
  bool active_player_on_taxi{false};
  bool active_player_can_auto_sit{false};
  bool local_afk_display_active{false};
  bool logout_request_active{false};
};

struct IdleBillingUpdateResult {
  bool fire_billing_nag_dialog{false};
  int billing_nag_minutes{0};
  bool fire_igr_billing_nag_dialog{false};
  bool show_billing_chat_warning{false};
  int billing_chat_warning_minutes{0};
  bool mark_afk{false};
  bool sit_for_afk{false};
  bool show_idle_logout_message{false};
  bool request_idle_logout{false};
};

class IdleBillingController {
 public:
  static constexpr std::uint32_t kAfkThresholdMs = 300000;
  static constexpr std::uint32_t kIdleLogoutThresholdMs = 1800000;

  void Reset();
  void ResetForWorldEntry(std::uint32_t now_ms, WorldBillingState billing);
  void NoteUserActivity(std::uint32_t now_ms);

  [[nodiscard]] IdleBillingUpdateResult Update(const IdleBillingUpdateContext &context);

  [[nodiscard]] std::uint32_t last_user_activity_tick() const {
    return last_user_activity_tick_;
  }

 private:
  [[nodiscard]] static int ComputeRemainingMinutes(std::uint32_t remaining_ms);
  [[nodiscard]] static std::uint32_t ComputeNextBillingWarningTick(std::uint32_t now_ms,
                                                                   std::uint32_t deadline_ms,
                                                                   int remaining_minutes);

  std::uint32_t last_user_activity_tick_{0};
  std::uint32_t billing_deadline_tick_{0};
  std::uint32_t billing_next_warning_tick_{0};
  bool billing_dialog_shown_{false};
  bool igr_billing_mode_{false};
};

}

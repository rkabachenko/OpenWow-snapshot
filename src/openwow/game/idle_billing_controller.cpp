#include "openwow/game/idle_billing_controller.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kMaxBillingMinutes = 0x10E0;
constexpr std::uint32_t kBillingExcludedFlag = 0x02;
constexpr std::uint32_t kIgrBillingFlag = 0x08;
constexpr std::uint32_t kIgrBillingDialogThresholdMinutes = 0x258;
constexpr std::uint32_t kBillingDialogThresholdMinutes = 30;
constexpr std::uint32_t kFrequentBillingWarningMinutes = 5;
constexpr std::uint32_t kMinuteMs = 60000;
constexpr std::uint32_t kFiveMinutesMs = 300000;

}

void IdleBillingController::Reset() {
  last_user_activity_tick_ = 0;
  billing_deadline_tick_ = 0;
  billing_next_warning_tick_ = 0;
  billing_dialog_shown_ = false;
  igr_billing_mode_ = false;
}

void IdleBillingController::ResetForWorldEntry(
    const std::uint32_t now_ms, const WorldBillingState billing) {
  Reset();
  last_user_activity_tick_ = now_ms;

  const std::uint32_t billing_minutes = billing.time_remaining_minutes;
  const std::uint8_t billing_flags = billing.flags;
  if (billing_minutes == 0 || billing_minutes > kMaxBillingMinutes ||
      (billing_flags & kBillingExcludedFlag) != 0) {
    return;
  }

  billing_deadline_tick_ = now_ms + billing_minutes * kMinuteMs;
  billing_next_warning_tick_ = 0;
  billing_dialog_shown_ = false;
  igr_billing_mode_ = (billing_flags & kIgrBillingFlag) != 0;
}

void IdleBillingController::NoteUserActivity(const std::uint32_t now_ms) {
  last_user_activity_tick_ = now_ms;
}

IdleBillingUpdateResult IdleBillingController::Update(const IdleBillingUpdateContext &context) {
  IdleBillingUpdateResult result;

  if (billing_deadline_tick_ != 0) {
    if (context.now_ms >= billing_deadline_tick_) {
      billing_deadline_tick_ = 0;
    } else {
      const std::uint32_t remaining_ms = billing_deadline_tick_ - context.now_ms;
      const int remaining_minutes = ComputeRemainingMinutes(remaining_ms);
      const bool warning_tick_reached =
          billing_next_warning_tick_ == 0 || context.now_ms >= billing_next_warning_tick_;

      if (warning_tick_reached) {
        if (igr_billing_mode_) {
          if (remaining_minutes <= static_cast<int>(kIgrBillingDialogThresholdMinutes) &&
              !billing_dialog_shown_) {
            result.fire_igr_billing_nag_dialog = true;
            billing_dialog_shown_ = true;
          }

          if (billing_next_warning_tick_ == 0) {
            billing_next_warning_tick_ = context.now_ms;
          }
        } else {
          if (remaining_minutes <= static_cast<int>(kBillingDialogThresholdMinutes)) {
            result.show_billing_chat_warning = true;
            result.billing_chat_warning_minutes = remaining_minutes;

            if (!billing_dialog_shown_) {
              result.fire_billing_nag_dialog = true;
              result.billing_nag_minutes = remaining_minutes;
              billing_dialog_shown_ = true;
            }
          }

          billing_next_warning_tick_ =
              ComputeNextBillingWarningTick(context.now_ms, billing_deadline_tick_,
                                            remaining_minutes);
        }
      }
    }
  }

  if (last_user_activity_tick_ == 0) {
    return result;
  }

  const std::uint32_t idle_ms = context.now_ms - last_user_activity_tick_;
  if (idle_ms < kAfkThresholdMs) {
    return result;
  }

  if (idle_ms >= kIdleLogoutThresholdMs) {
    if (!context.logout_request_active) {
      result.show_idle_logout_message = true;
      result.request_idle_logout = true;
    }
    return result;
  }

  if (!context.has_active_player) {
    return result;
  }

  if (context.local_afk_display_active || context.active_player_on_taxi) {
    return result;
  }

  result.mark_afk = true;
  result.sit_for_afk = context.active_player_can_auto_sit;
  return result;
}

int IdleBillingController::ComputeRemainingMinutes(const std::uint32_t remaining_ms) {
  return static_cast<int>((remaining_ms + (kMinuteMs - 1)) / kMinuteMs);
}

std::uint32_t IdleBillingController::ComputeNextBillingWarningTick(
    const std::uint32_t now_ms, const std::uint32_t deadline_ms, const int remaining_minutes) {
  const std::uint32_t remaining_ms = deadline_ms - now_ms;
  const std::uint32_t interval_ms =
      remaining_minutes <= static_cast<int>(kFrequentBillingWarningMinutes) ? kMinuteMs
                                                                             : kFiveMinutesMs;
  return now_ms + remaining_ms - interval_ms * (remaining_ms / interval_ms);
}

}

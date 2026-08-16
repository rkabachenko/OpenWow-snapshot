#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"

#include "openwow/game/duel_system.h"

#include "openwow/game/localization.h"

#include <cmath>
#include <cstdio>

namespace openwow::game {

namespace {

constexpr float kCountdownTickIntervalSeconds = 1.0f;

}

void DuelSystem::Challenge(ObjectGuid targetGuid,
                           const std::string& targetName) {
  state_ = DuelState::Challenged;
  opponent_guid_ = targetGuid;
  opponent_name_ = targetName;
  flag_guid_ = ObjectGuid{};
  is_challenger_ = true;
  result_ = DuelResult::None;
  win_type_ = DuelWinType::Knockout;
  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
  last_countdown_phase_ = -1;
  oob_active_ = false;
  oob_timer_ = 0.0f;
  oob_timer_expired_ = false;
  winner_name_.clear();
  loser_name_.clear();
  health_snapshot_ = {};
}

void DuelSystem::ReceiveChallenge(ObjectGuid challengerGuid,
                                  const std::string& challengerName,
                                  ObjectGuid flagGuid) {
  state_ = DuelState::Pending;
  opponent_guid_ = challengerGuid;
  opponent_name_ = challengerName;
  flag_guid_ = flagGuid;
  is_challenger_ = false;
  result_ = DuelResult::None;
  win_type_ = DuelWinType::Knockout;
  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
  last_countdown_phase_ = -1;
  oob_active_ = false;
  oob_timer_ = 0.0f;
  oob_timer_expired_ = false;
  winner_name_.clear();
  loser_name_.clear();
  health_snapshot_ = {};
}

void DuelSystem::ReceiveChallenge(ObjectGuid challengerGuid,
                                  const std::string& challengerName) {
  ReceiveChallenge(challengerGuid, challengerName, ObjectGuid{});
}

void DuelSystem::Accept() {
  if (state_ == DuelState::Pending || state_ == DuelState::Challenged) {

    state_ = DuelState::Countdown;
    countdown_ = kDefaultCountdown;
    CancelCountdownMessageSchedule();
    last_countdown_phase_ = static_cast<int>(std::ceil(kDefaultCountdown));
    oob_active_ = false;
    oob_timer_ = 0.0f;
    oob_timer_expired_ = false;
  }
}

void DuelSystem::Decline() {
  if (state_ == DuelState::Pending || state_ == DuelState::Challenged) {
    Reset();
  }
}

void DuelSystem::Cancel() {
  if (state_ != DuelState::None && state_ != DuelState::Complete) {
    Reset();
  }
}

void DuelSystem::StartCountdown(std::uint32_t countdown_ms) {
  state_ = DuelState::Countdown;
  countdown_ = static_cast<float>(countdown_ms) / 1000.0f;
  CancelCountdownMessageSchedule();
  countdown_integer_ = static_cast<int>(countdown_ms / 1000u);
  DispatchCountdownMessageTick();
  last_countdown_phase_ = static_cast<int>(std::ceil(countdown_));
  oob_active_ = false;
  oob_timer_ = 0.0f;
  oob_timer_expired_ = false;
}

void DuelSystem::NotifyOutOfBounds() {
  if (state_ == DuelState::InProgress) {
    oob_active_ = true;
    oob_timer_ = kOOBForfeitTime;
    oob_timer_expired_ = false;
    if (on_out_of_bounds_) on_out_of_bounds_();
  }
}

void DuelSystem::NotifyInBounds() {
  if (state_ == DuelState::InProgress) {
    oob_active_ = false;
    oob_timer_ = 0.0f;
    oob_timer_expired_ = false;
    if (on_in_bounds_) on_in_bounds_();
  }
}

void DuelSystem::CompleteDuel(bool canceled) {

  if (canceled) {
    Reset();
    return;
  }

  state_ = DuelState::Complete;
  opponent_guid_ = ObjectGuid{};
  flag_guid_ = ObjectGuid{};
  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
  oob_active_ = false;
  oob_timer_ = 0.0f;
  oob_timer_expired_ = false;
  if (on_duel_complete_) on_duel_complete_();
}

void DuelSystem::SetWinner(DuelWinType winType,
                           const std::string& winnerName,
                           const std::string& loserName) {
  win_type_ = winType;
  winner_name_ = winnerName;
  loser_name_ = loserName;

  if (winType == DuelWinType::Retreat) {
    result_ = DuelResult::Fled;
  } else {

    result_ = DuelResult::Won;
  }
}

void DuelSystem::EndDuel(DuelResult result) {
  if (result == DuelResult::Won || result == DuelResult::Lost ||
      result == DuelResult::Fled) {
    result_ = result;
    state_ = DuelState::Complete;
    opponent_guid_ = ObjectGuid{};
    flag_guid_ = ObjectGuid{};
    countdown_ = 0.0f;
    CancelCountdownMessageSchedule();
    oob_active_ = false;
    oob_timer_ = 0.0f;
    oob_timer_expired_ = false;
    if (on_duel_complete_) on_duel_complete_();
  }
}

bool DuelSystem::IsOutOfBounds(float playerX, float playerY) const {
  float dx = playerX - boundary_x_;
  float dy = playerY - boundary_y_;
  float distSq = dx * dx + dy * dy;
  return distSq > (kBoundaryRadius * kBoundaryRadius);
}

float DuelSystem::GetDistanceSqFromFlag(float px, float py) const {
  float dx = px - boundary_x_;
  float dy = py - boundary_y_;
  return dx * dx + dy * dy;
}

int DuelSystem::GetCountdownPhase() const {
  if (state_ != DuelState::Countdown && state_ != DuelState::InProgress)
    return -1;
  if (countdown_ <= 0.0f) return 0;
  return static_cast<int>(std::ceil(countdown_));
}

std::string DuelSystem::GetCountdownText() const {
  int phase = GetCountdownPhase();
  if (phase < 0) return "";
  if (phase == 0) return "DUEL!";
  return std::to_string(phase);
}

std::string DuelSystem::GetOOBWarningText() const {
  if (state_ != DuelState::InProgress || !oob_active_ || oob_timer_ <= 0.0f)
    return "";
  int seconds = static_cast<int>(std::ceil(oob_timer_));
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Leaving duel area (%ds)", seconds);
  return buf;
}

void DuelSystem::SaveHealthSnapshot(std::uint32_t health,
                                    std::uint32_t maxHealth,
                                    std::uint32_t mana,
                                    std::uint32_t maxMana) {
  health_snapshot_.health     = health;
  health_snapshot_.max_health = maxHealth;
  health_snapshot_.mana       = mana;
  health_snapshot_.max_mana   = maxMana;
}

std::string DuelSystem::GetVictoryMessage() const {
  if (winner_name_.empty() || loser_name_.empty()) return "";
  return winner_name_ + " has defeated " + loser_name_ + " in a duel";
}

std::string DuelSystem::GetWinnerEventName() const {
  return (win_type_ == DuelWinType::Retreat)
             ? "DUEL_WINNER_RETREAT"
             : "DUEL_WINNER_KNOCKOUT";
}

std::string DuelSystem::GetWinnerAnnouncement() const {
  if (winner_name_.empty() || loser_name_.empty()) {
    return {};
  }

  const std::string key = GetWinnerEventName();
  const std::string format = Localization::Get().GetString(key, key);
  return Localization::Get().FormatString(format, {winner_name_, loser_name_});
}

void DuelSystem::Update(float dt) {
  if (state_ == DuelState::Countdown) {
    EmitScheduledCountdownMessages(dt);

    if (countdown_ > 0.0f) {
      float prev = countdown_;
      countdown_ -= dt;
      if (countdown_ < 0.0f) countdown_ = 0.0f;

      int newPhase = GetCountdownPhase();
      if (newPhase != last_countdown_phase_ && newPhase >= 0) {
        last_countdown_phase_ = newPhase;
        if (on_countdown_tick_) on_countdown_tick_();
      }

      if (prev > 0.0f && countdown_ <= 0.0f) {
        state_ = DuelState::InProgress;
        countdown_ = 0.0f;
        if (on_countdown_finished_) on_countdown_finished_();
      }
    }
  }

  if (state_ == DuelState::InProgress) {

    if (oob_active_ && oob_timer_ > 0.0f) {
      oob_timer_ -= dt;
      if (oob_timer_ <= 0.0f) {
        oob_timer_ = 0.0f;
        oob_timer_expired_ = true;
        if (on_forfeit_) on_forfeit_();
      }
    }
  }
}

void DuelSystem::Reset() {
  state_ = DuelState::None;
  result_ = DuelResult::None;
  win_type_ = DuelWinType::Knockout;
  opponent_guid_ = ObjectGuid{};
  opponent_name_.clear();
  is_challenger_ = false;
  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
  last_countdown_phase_ = -1;
  boundary_x_ = 0.0f;
  boundary_y_ = 0.0f;
  boundary_z_ = 0.0f;
  oob_active_ = false;
  oob_timer_ = 0.0f;
  oob_timer_expired_ = false;
  flag_guid_ = ObjectGuid{};
  health_snapshot_ = {};
  winner_name_.clear();
  loser_name_.clear();

}

DuelSystem::CountdownTickResult DuelSystem::FormatCountdownTick() {
  CountdownTickResult result;
  const std::string format =
      Localization::Get().GetString("DUEL_COUNTDOWN", "DUEL_COUNTDOWN");
  result.message = Localization::Get().FormatString(
      format, {std::to_string(countdown_integer_)});

  --countdown_integer_;
  result.schedule_next = countdown_integer_ != 0;

  return result;
}

void DuelSystem::ResetCountdownState() {
  flag_guid_ = ObjectGuid{};
  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
}

DuelSystem::DuelCompleteResult DuelSystem::HandleDuelCompletePacket(
    const uint8_t* data, size_t len) {
  DuelCompleteResult result{};
  if (!data || len < 1) return result;

  result.canceled = (data[0] != 0);

  if (!flag_guid_.IsEmpty()) {
    result.was_in_duel = true;
    result.show_system_msg = !result.canceled;
    flag_guid_ = ObjectGuid{};
  }

  countdown_ = 0.0f;
  CancelCountdownMessageSchedule();
  opponent_guid_ = ObjectGuid{};

  return result;
}

void DuelSystem::CancelCountdownMessageSchedule() {
  countdown_integer_ = 0;
  countdown_message_elapsed_ = 0.0f;
  countdown_message_scheduled_ = false;
}

void DuelSystem::EmitScheduledCountdownMessages(float dt) {
  if (!countdown_message_scheduled_ || dt <= 0.0f) {
    return;
  }

  countdown_message_elapsed_ += dt;
  while (countdown_message_scheduled_ &&
         countdown_message_elapsed_ >= kCountdownTickIntervalSeconds) {
    countdown_message_elapsed_ -= kCountdownTickIntervalSeconds;
    DispatchCountdownMessageTick();
  }
}

void DuelSystem::DispatchCountdownMessageTick() {
  const CountdownTickResult tick = FormatCountdownTick();
  if (on_countdown_message_) {
    on_countdown_message_(tick.message);
  }

  countdown_message_scheduled_ = tick.schedule_next;
}

}


#include "openwow/game/bg/bg_warsong_gulch.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace openwow::game {

bool BgWarsongGulch::IsRelevantWorldState(std::int32_t ws_id) {
  switch (ws_id) {
    case kWsAllianceScore:
    case kWsHordeScore:
    case kWsAllianceFlagState:
    case kWsHordeFlagState:
    case kWsAllianceFlagCarrier:
    case kWsHordeFlagCarrier:
    case kWsTimeRemaining:
      return true;
    default:
      return false;
  }
}

void BgWarsongGulch::OnWorldStateUpdate(std::int32_t ws_id,
                                         std::int32_t value) {
  switch (ws_id) {
    case kWsAllianceScore:
      alliance_score_ = std::clamp(value, 0, kMaxCaptures);
      break;

    case kWsHordeScore:
      horde_score_ = std::clamp(value, 0, kMaxCaptures);
      break;

    case kWsAllianceFlagState:
      if (value >= 1 && value <= 3)
        alliance_flag_state_ = static_cast<WsgFlagState>(value);
      else
        alliance_flag_state_ = WsgFlagState::kUnknown;
      break;

    case kWsHordeFlagState:
      if (value >= 1 && value <= 3)
        horde_flag_state_ = static_cast<WsgFlagState>(value);
      else
        horde_flag_state_ = WsgFlagState::kUnknown;
      break;

    case kWsAllianceFlagCarrier:
      alliance_flag_carrier_ = static_cast<std::uint32_t>(value);
      break;

    case kWsHordeFlagCarrier:
      horde_flag_carrier_ = static_cast<std::uint32_t>(value);
      break;

    case kWsTimeRemaining:
      remaining_time_ = static_cast<float>(value);
      break;

    default:
      break;
  }
}

void BgWarsongGulch::Update(float dt) {
  if (remaining_time_ > 0.0f && !IsFinished()) {
    remaining_time_ = std::max(0.0f, remaining_time_ - dt);
  }
}

bool BgWarsongGulch::IsFinished() const {
  return alliance_score_ >= kMaxCaptures || horde_score_ >= kMaxCaptures;
}

std::string BgWarsongGulch::GetScoreText() const {
  return "Alliance " + std::to_string(alliance_score_) + " - " +
         std::to_string(horde_score_) + " Horde";
}

std::string BgWarsongGulch::GetFlagStateText(WsgFlagState state) {
  switch (state) {
    case WsgFlagState::kInBase:   return "In Base";
    case WsgFlagState::kCarried:  return "Carried";
    case WsgFlagState::kOnGround: return "Dropped";
    case WsgFlagState::kUnknown:  return "Unknown";
  }
  return "Invalid";
}

std::string BgWarsongGulch::GetFormattedTimeRemaining() const {
  int total_sec = static_cast<int>(remaining_time_);
  if (total_sec < 0) total_sec = 0;
  int minutes = total_sec / 60;
  int seconds = total_sec % 60;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
  return buf;
}

int BgWarsongGulch::GetWinner() const {
  if (alliance_score_ >= kMaxCaptures) return 1;
  if (horde_score_ >= kMaxCaptures)    return 2;
  return 0;
}

std::string BgWarsongGulch::GetStatusText() const {
  std::string status = GetScoreText() + "  Time: " + GetFormattedTimeRemaining();
  status += "  A-Flag: " + GetFlagStateText(alliance_flag_state_);
  status += "  H-Flag: " + GetFlagStateText(horde_flag_state_);
  if (IsFinished()) {
    int winner = GetWinner();
    status += (winner == 1) ? "  [ALLIANCE WINS]" : "  [HORDE WINS]";
  }
  return status;
}

bool BgWarsongGulch::AreBothFlagsInBase() const {
  return alliance_flag_state_ == WsgFlagState::kInBase &&
         horde_flag_state_ == WsgFlagState::kInBase;
}

void BgWarsongGulch::Reset() {
  alliance_flag_state_ = WsgFlagState::kInBase;
  horde_flag_state_ = WsgFlagState::kInBase;
  alliance_flag_carrier_ = 0;
  horde_flag_carrier_ = 0;
  alliance_score_ = 0;
  horde_score_ = 0;
  remaining_time_ = kMatchDuration;
}

}

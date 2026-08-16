
#include "openwow/game/bg/bg_eye_of_the_storm.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace openwow::game {

bool BgEyeOfTheStorm::IsRelevantWorldState(std::int32_t ws_id) {
  if (ws_id == kWsAllianceScore || ws_id == kWsHordeScore ||
      ws_id == kWsAllianceBaseCount || ws_id == kWsHordeBaseCount ||
      ws_id == kWsFlagAtCenter || ws_id == kWsFlagStateAlliance ||
      ws_id == kWsFlagStateHorde ||
      ws_id == kWsProgressBarShow || ws_id == kWsProgressBarStatus ||
      ws_id == kWsProgressBarGrey) {
    return true;
  }

  for (int t = 0; t < 4; ++t) {
    if (ws_id == kTowerIconWS[t].neutral ||
        ws_id == kTowerIconWS[t].alliance_controlled ||
        ws_id == kTowerIconWS[t].horde_controlled) {
      return true;
    }
  }

  for (int t = 0; t < 4; ++t) {
    if (ws_id == kTowerConflictWS[t].alliance_conflict ||
        ws_id == kTowerConflictWS[t].horde_conflict) {
      return true;
    }
  }

  return false;
}

void BgEyeOfTheStorm::OnWorldStateUpdate(std::int32_t ws_id,
                                          std::int32_t value) {

  if (ws_id == kWsAllianceScore) {
    alliance_score_ = std::clamp(value, 0, kMaxScore);
    return;
  }
  if (ws_id == kWsHordeScore) {
    horde_score_ = std::clamp(value, 0, kMaxScore);
    return;
  }

  if (ws_id == kWsAllianceBaseCount) {
    alliance_base_count_ = std::max(0, value);
    return;
  }
  if (ws_id == kWsHordeBaseCount) {
    horde_base_count_ = std::max(0, value);
    return;
  }

  if (ws_id == kWsFlagAtCenter) {
    flag_at_center_ = (value != 0);
    return;
  }
  if (ws_id == kWsFlagStateAlliance) {
    flag_state_alliance_ = static_cast<std::uint8_t>(std::clamp(value, 0, 2));
    return;
  }
  if (ws_id == kWsFlagStateHorde) {
    flag_state_horde_ = static_cast<std::uint8_t>(std::clamp(value, 0, 2));
    return;
  }

  if (ws_id == kWsProgressBarShow) {
    progress_bar_shown_ = (value != 0);
    return;
  }
  if (ws_id == kWsProgressBarStatus) {
    progress_bar_status_ = value;
    return;
  }

  if (value == 1) {
    for (int t = 0; t < 4; ++t) {
      if (ws_id == kTowerIconWS[t].neutral) {
        towers_[t] = EotsTowerState::kNeutral;
        return;
      }
      if (ws_id == kTowerIconWS[t].alliance_controlled) {
        towers_[t] = EotsTowerState::kAlliance;
        return;
      }
      if (ws_id == kTowerIconWS[t].horde_controlled) {
        towers_[t] = EotsTowerState::kHorde;
        return;
      }
    }
  }

}

void BgEyeOfTheStorm::Update(float dt) {
  if (remaining_time_ > 0.0f && !IsFinished()) {
    remaining_time_ = std::max(0.0f, remaining_time_ - dt);
  }
}

EotsTowerState BgEyeOfTheStorm::GetTowerState(EotsTower tower) const {
  auto idx = static_cast<std::size_t>(tower);
  if (idx >= towers_.size()) return EotsTowerState::kUnknown;
  return towers_[idx];
}

int BgEyeOfTheStorm::GetTowersControlledByAlliance() const {
  int count = 0;
  for (auto s : towers_) {
    if (s == EotsTowerState::kAlliance) ++count;
  }
  return count;
}

int BgEyeOfTheStorm::GetTowersControlledByHorde() const {
  int count = 0;
  for (auto s : towers_) {
    if (s == EotsTowerState::kHorde) ++count;
  }
  return count;
}

EotsFlagCarrier BgEyeOfTheStorm::GetFlagCarrier() const {
  if (flag_state_alliance_ == 2) return EotsFlagCarrier::kAlliance;
  if (flag_state_horde_ == 2)    return EotsFlagCarrier::kHorde;
  return EotsFlagCarrier::kNone;
}

bool BgEyeOfTheStorm::IsFinished() const {
  return alliance_score_ >= kMaxScore || horde_score_ >= kMaxScore;
}

std::string BgEyeOfTheStorm::GetScoreText() const {
  return "Alliance " + std::to_string(alliance_score_) + " - " +
         std::to_string(horde_score_) + " Horde";
}

std::string BgEyeOfTheStorm::GetFormattedTimeRemaining() const {
  int total_sec = static_cast<int>(remaining_time_);
  if (total_sec < 0) total_sec = 0;
  int minutes = total_sec / 60;
  int seconds = total_sec % 60;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
  return buf;
}

std::string BgEyeOfTheStorm::GetStatusText() const {
  std::string status = GetScoreText();

  status += "  Towers: A=" + std::to_string(GetTowersControlledByAlliance()) +
            " H=" + std::to_string(GetTowersControlledByHorde());

  status += "  Flag: ";
  switch (GetFlagCarrier()) {
    case EotsFlagCarrier::kAlliance: status += "Carried(A)"; break;
    case EotsFlagCarrier::kHorde:    status += "Carried(H)"; break;
    default:
      status += flag_at_center_ ? "Center" : "Dropped";
      break;
  }

  if (IsFinished()) {
    int winner = GetWinner();
    status += (winner == 1) ? " [ALLIANCE WINS]" : " [HORDE WINS]";
  }
  return status;
}

int BgEyeOfTheStorm::GetWinner() const {
  if (alliance_score_ >= kMaxScore) return 1;
  if (horde_score_ >= kMaxScore)    return 2;
  return 0;
}

std::string_view BgEyeOfTheStorm::GetTowerName(EotsTower tower) {
  switch (tower) {
    case EotsTower::kFelReaver:    return "Fel Reaver Ruins";
    case EotsTower::kBloodElf:     return "Blood Elf Tower";
    case EotsTower::kDraeneiRuins: return "Draenei Ruins";
    case EotsTower::kMageTower:    return "Mage Tower";
    default:                       return "Unknown Tower";
  }
}

std::string BgEyeOfTheStorm::GetFlagCarrierText(EotsFlagCarrier carrier) {
  switch (carrier) {
    case EotsFlagCarrier::kAlliance: return "Alliance";
    case EotsFlagCarrier::kHorde:    return "Horde";
    default:                         return "None";
  }
}

void BgEyeOfTheStorm::Reset() {
  alliance_score_ = 0;
  horde_score_ = 0;
  alliance_base_count_ = 0;
  horde_base_count_ = 0;
  towers_.fill(EotsTowerState::kNeutral);
  flag_at_center_ = true;
  flag_state_alliance_ = 1;
  flag_state_horde_ = 1;
  progress_bar_status_ = 50;
  progress_bar_shown_ = false;
  remaining_time_ = kMatchDuration;
}

}

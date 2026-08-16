
#include "openwow/game/bg/bg_strand_of_the_ancients.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace openwow::game {

bool BgStrandOfTheAncients::IsRelevantWorldState(std::int32_t ws_id) {
  switch (ws_id) {
    case kWsAllianceAttacks:
    case kWsHordeAttacks:
    case kWsTimerMins:
    case kWsTimerSecTens:
    case kWsTimerSecDecs:
    case kWsBonusTimer:
    case kWsEnableTimer:
    case kWsRightAttTokenAll:
    case kWsLeftAttTokenAll:
    case kWsLeftAttTokenHorde:
    case kWsRightAttTokenHorde:
    case kWsAllianceDefToken:
    case kWsHordeDefToken:
    case kWsLeftGYAlliance:
    case kWsCenterGYAlliance:
    case kWsRightGYAlliance:
    case kWsLeftGYHorde:
    case kWsCenterGYHorde:
    case kWsRightGYHorde:
      return true;
    default:
      break;
  }

  for (int g = 0; g < 6; ++g) {
    if (ws_id == kGateWS[g]) return true;
  }

  return false;
}

void BgStrandOfTheAncients::OnWorldStateUpdate(std::int32_t ws_id,
                                                std::int32_t value) {

  if (ws_id == kWsAllianceAttacks) {
    alliance_attacking_ = (value != 0);
    return;
  }
  if (ws_id == kWsHordeAttacks) {
    horde_attacking_ = (value != 0);
    return;
  }

  for (int g = 0; g < 6; ++g) {
    if (ws_id == kGateWS[g]) {
      if (value >= 1 && value <= 3)
        gates_[g] = static_cast<SotaGateState>(value);
      else
        gates_[g] = SotaGateState::kUnknown;
      return;
    }
  }

  if (ws_id == kWsTimerMins) {
    timer_mins_ = std::max(0, value);
    return;
  }
  if (ws_id == kWsTimerSecTens) {
    timer_sec_tens_ = std::clamp(value, 0, 9);
    return;
  }
  if (ws_id == kWsTimerSecDecs) {
    timer_sec_decs_ = std::clamp(value, 0, 9);
    return;
  }
  if (ws_id == kWsBonusTimer) {
    bonus_round_ = (value != 0);
    return;
  }
  if (ws_id == kWsEnableTimer) {
    timer_enabled_ = (value != 0);
    return;
  }

  if (ws_id == kWsLeftGYAlliance) {
    graveyards_[static_cast<int>(SotaGraveyard::kLeft)] =
        (value != 0) ? SotaGraveyardControl::kAlliance : SotaGraveyardControl::kNeutral;
    return;
  }
  if (ws_id == kWsCenterGYAlliance) {
    graveyards_[static_cast<int>(SotaGraveyard::kCenter)] =
        (value != 0) ? SotaGraveyardControl::kAlliance : SotaGraveyardControl::kNeutral;
    return;
  }
  if (ws_id == kWsRightGYAlliance) {
    graveyards_[static_cast<int>(SotaGraveyard::kRight)] =
        (value != 0) ? SotaGraveyardControl::kAlliance : SotaGraveyardControl::kNeutral;
    return;
  }
  if (ws_id == kWsLeftGYHorde) {
    if (value != 0)
      graveyards_[static_cast<int>(SotaGraveyard::kLeft)] = SotaGraveyardControl::kHorde;
    return;
  }
  if (ws_id == kWsCenterGYHorde) {
    if (value != 0)
      graveyards_[static_cast<int>(SotaGraveyard::kCenter)] = SotaGraveyardControl::kHorde;
    return;
  }
  if (ws_id == kWsRightGYHorde) {
    if (value != 0)
      graveyards_[static_cast<int>(SotaGraveyard::kRight)] = SotaGraveyardControl::kHorde;
    return;
  }

}

void BgStrandOfTheAncients::Update(float ) {

}

SotaGateState BgStrandOfTheAncients::GetGateState(SotaGate gate) const {
  auto idx = static_cast<std::size_t>(gate);
  if (idx >= gates_.size()) return SotaGateState::kUnknown;
  return gates_[idx];
}

int BgStrandOfTheAncients::GetGateStateValue(SotaGate gate) const {
  return static_cast<int>(GetGateState(gate));
}

SotaGraveyardControl BgStrandOfTheAncients::GetGraveyardControl(
    SotaGraveyard gy) const {
  auto idx = static_cast<std::size_t>(gy);
  if (idx >= graveyards_.size()) return SotaGraveyardControl::kNeutral;
  return graveyards_[idx];
}

int BgStrandOfTheAncients::GetAttackingFaction() const {
  if (alliance_attacking_) return 0;
  if (horde_attacking_)    return 1;
  return -1;
}

int BgStrandOfTheAncients::GetTotalRoundTimeRemainingSec() const {
  return timer_mins_ * 60 + timer_sec_tens_ * 10 + timer_sec_decs_;
}

std::string BgStrandOfTheAncients::GetFormattedTimeRemaining() const {
  int total_sec = GetTotalRoundTimeRemainingSec();
  if (total_sec < 0) total_sec = 0;
  int minutes = total_sec / 60;
  int seconds = total_sec % 60;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
  return buf;
}

bool BgStrandOfTheAncients::IsFinished() const {

  return false;
}

std::string BgStrandOfTheAncients::GetStatusText() const {
  std::string status;
  if (alliance_attacking_) {
    status = "Alliance attacking";
  } else if (horde_attacking_) {
    status = "Horde attacking";
  } else {
    status = "No faction attacking";
  }

  if (bonus_round_) status += " [BONUS ROUND]";

  status += "  Gates:";
  for (int g = 0; g < 6; ++g) {
    status += " " + std::to_string(static_cast<int>(gates_[g]));
  }

  status += "  GYs:";
  for (int g = 0; g < 3; ++g) {
    status += " " + std::to_string(static_cast<int>(graveyards_[g]));
  }

  if (timer_enabled_) {
    status += "  Time: " + GetFormattedTimeRemaining();
  }

  return status;
}

std::string_view BgStrandOfTheAncients::GetGateName(SotaGate gate) {
  switch (gate) {
    case SotaGate::kGreen:   return "Green Gate";
    case SotaGate::kBlue:    return "Blue Gate";
    case SotaGate::kRed:     return "Red Gate";
    case SotaGate::kPurple:  return "Purple Gate";
    case SotaGate::kYellow:  return "Yellow Gate";
    case SotaGate::kAncient: return "Ancient Gate";
    default:                 return "Unknown Gate";
  }
}

std::string_view BgStrandOfTheAncients::GetGraveyardName(SotaGraveyard gy) {
  switch (gy) {
    case SotaGraveyard::kLeft:   return "Left Graveyard";
    case SotaGraveyard::kCenter: return "Center Graveyard";
    case SotaGraveyard::kRight:  return "Right Graveyard";
    default:                     return "Unknown Graveyard";
  }
}

std::string_view BgStrandOfTheAncients::GetGateStateName(SotaGateState state) {
  switch (state) {
    case SotaGateState::kOk:        return "Intact";
    case SotaGateState::kDamaged:   return "Damaged";
    case SotaGateState::kDestroyed: return "Destroyed";
    default:                        return "Unknown";
  }
}

void BgStrandOfTheAncients::Reset() {
  alliance_attacking_ = false;
  horde_attacking_ = false;
  gates_.fill(SotaGateState::kUnknown);
  graveyards_.fill(SotaGraveyardControl::kNeutral);
  timer_enabled_ = false;
  bonus_round_ = false;
  timer_mins_ = 0;
  timer_sec_tens_ = 0;
  timer_sec_decs_ = 0;
}

}

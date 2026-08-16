
#include "openwow/game/pvp_flag_manager.h"

#include <algorithm>

namespace openwow::game {

namespace {

constexpr std::uint32_t kColorYellow  = 0xFFFFFF00;
constexpr std::uint32_t kColorRed     = 0xFFFF0000;
constexpr std::uint32_t kColorBlue    = 0xFF69CCF0;

}

void PvPFlagManager::SetFlagged(bool flagged) {
  flagged_ = flagged;

  if (!flagged_) {
    auto_flag_timer_ = 0.0f;
  }
}

void PvPFlagManager::SetFFA(bool ffa) {
  ffa_ = ffa;

  if (ffa_) {
    auto_flag_timer_ = 0.0f;
  }
}

PvPFlagState PvPFlagManager::GetState() const {

  if (in_sanctuary_) return PvPFlagState::Off;

  if (ffa_) return PvPFlagState::FFA;

  if (flagged_ || in_pvp_zone_) return PvPFlagState::PvP;

  if (auto_flag_timer_ > 0.0f) return PvPFlagState::PvP;

  return PvPFlagState::Off;
}

void PvPFlagManager::TogglePvP() {

  if (in_sanctuary_) return;

  if (flagged_ && in_pvp_zone_) return;

  if (flagged_) {

    flagged_ = false;
    auto_flag_timer_ = kDefaultAutoTimer;
  } else {

    flagged_ = true;

    auto_flag_timer_ = 0.0f;
  }
}

std::uint32_t PvPFlagManager::GetFlagColor() const {

  if (in_sanctuary_) return kColorBlue;

  if (ffa_ || flagged_) return kColorRed;

  return kColorYellow;
}

void PvPFlagManager::Update(float dt) {

  if (ffa_) return;

  if (in_sanctuary_) return;

  if (in_pvp_zone_ && !flagged_) {
    flagged_ = true;
    auto_flag_timer_ = 0.0f;
  }

  if (auto_flag_timer_ > 0.0f) {

    if (!in_pvp_zone_) {
      auto_flag_timer_ -= dt;
      if (auto_flag_timer_ <= 0.0f) {
        auto_flag_timer_ = 0.0f;
        flagged_ = false;
      }
    }
  }
}

void PvPFlagManager::Reset() {
  flagged_         = false;
  ffa_             = false;
  in_pvp_zone_     = false;
  in_sanctuary_    = false;
  auto_flag_timer_ = 0.0f;
}

}

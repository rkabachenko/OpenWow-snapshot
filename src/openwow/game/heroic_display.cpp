
#include "openwow/game/heroic_display.h"

#include <cstdio>

namespace openwow::game {

void HeroicDisplay::SetHeroicInfo(const HeroicDisplayInfo& info) {
  std::lock_guard lock(mutex_);
  info_ = info;
}

std::optional<HeroicDisplayInfo> HeroicDisplay::GetHeroicInfo() const {
  std::lock_guard lock(mutex_);
  return info_;
}

bool HeroicDisplay::IsHeroic() const {
  std::lock_guard lock(mutex_);
  if (!info_) return false;
  return info_->isHeroicDungeon || info_->isHeroicRaid;
}

std::string HeroicDisplay::GetDifficultyLabel() const {
  std::lock_guard lock(mutex_);
  if (!info_) return "Normal";
  if (info_->isHeroicDungeon || info_->isHeroicRaid) return "Heroic";
  return "Normal";
}

std::string HeroicDisplay::GetPlayerCountLabel() const {
  std::lock_guard lock(mutex_);
  if (!info_) return "5 Player";
  return std::to_string(info_->playerCount) + " Player";
}

std::string HeroicDisplay::GetFullLabel() const {
  std::lock_guard lock(mutex_);
  if (!info_) return "5 Player (Normal)";

  std::string result =
      std::to_string(info_->playerCount) + " Player";

  if (info_->isHeroicDungeon || info_->isHeroicRaid) {
    result += " (Heroic)";
  } else {
    result += " (Normal)";
  }
  return result;
}

void HeroicDisplay::SetInInstance(bool inInstance) {
  std::lock_guard lock(mutex_);
  inInstance_ = inInstance;
}

bool HeroicDisplay::IsInInstance() const {
  std::lock_guard lock(mutex_);
  return inInstance_;
}

bool HeroicDisplay::ShowDifficultyFrame() const {
  std::lock_guard lock(mutex_);

  return inInstance_ && info_.has_value();
}

void HeroicDisplay::Reset() {
  std::lock_guard lock(mutex_);
  info_.reset();
  inInstance_ = false;
  locked_     = false;
  resetTime_  = 0;
}

uint8_t HeroicDisplay::GetDifficultyId() const {
  std::lock_guard lock(mutex_);
  if (!info_) return 0;

  if (info_->isHeroicRaid) {
    return (info_->playerCount >= 25) ? 5 : 4;
  }
  if (info_->isHeroicDungeon) return 1;
  if (info_->playerCount >= 25) return 3;
  if (info_->playerCount >= 10) return 2;
  return 0;
}

bool HeroicDisplay::IsRaid() const {
  std::lock_guard lock(mutex_);
  if (!info_) return false;
  return info_->playerCount > 5;
}

bool HeroicDisplay::IsDungeon() const {
  std::lock_guard lock(mutex_);
  if (!info_) return false;
  return info_->playerCount <= 5;
}

uint32_t HeroicDisplay::GetInstanceId() const {
  std::lock_guard lock(mutex_);
  if (!info_) return 0;
  return info_->instanceId;
}

std::string HeroicDisplay::GetInstanceName() const {
  std::lock_guard lock(mutex_);
  if (!info_) return "";
  return info_->instanceName;
}

uint8_t HeroicDisplay::GetPlayerCount() const {
  std::lock_guard lock(mutex_);
  if (!info_) return 0;
  return info_->playerCount;
}

bool HeroicDisplay::IsNormalDungeon() const {
  std::lock_guard lock(mutex_);
  if (!info_) return false;
  return info_->playerCount <= 5 && !info_->isHeroicDungeon;
}

std::string HeroicDisplay::GetTexturePath(bool isHeroic) {

  if (isHeroic) {
    return "Interface\\Common\\KeyRing";
  }
  return "Interface\\Common\\Indicator-Gray";
}

void HeroicDisplay::SetLocked(bool locked) {
  std::lock_guard lock(mutex_);
  locked_ = locked;
}

bool HeroicDisplay::IsLocked() const {
  std::lock_guard lock(mutex_);
  return locked_;
}

void HeroicDisplay::SetResetTime(uint32_t seconds) {
  std::lock_guard lock(mutex_);
  resetTime_ = seconds;
}

uint32_t HeroicDisplay::GetResetTime() const {
  std::lock_guard lock(mutex_);
  return resetTime_;
}

std::string HeroicDisplay::FormatResetTime(uint32_t seconds) {
  if (seconds == 0) return "";
  char buf[64];
  if (seconds >= 86400) {
    uint32_t d = seconds / 86400;
    uint32_t h = (seconds % 86400) / 3600;
    uint32_t m = (seconds % 3600) / 60;
    std::snprintf(buf, sizeof(buf), "%ud %uh %um", d, h, m);
  } else if (seconds >= 3600) {
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    std::snprintf(buf, sizeof(buf), "%uh %um", h, m);
  } else {
    uint32_t m = seconds / 60;
    std::snprintf(buf, sizeof(buf), "%um", m);
  }
  return std::string(buf);
}

}

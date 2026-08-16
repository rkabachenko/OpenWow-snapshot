
#include "openwow/game/spirit_healer.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void SpiritHealerSystem::SetNearSpiritHealer(bool near) {
  nearSpiritHealer_ = near;
}

bool SpiritHealerSystem::IsNearSpiritHealer() const {
  return nearSpiritHealer_;
}

void SpiritHealerSystem::AcceptSpiritHeal() { usedSpiritHealer_ = true; }

void SpiritHealerSystem::OfferResurrect(ResurrectOffer offer) {
  pendingOffer_ = std::move(offer);
}

std::optional<ResurrectOffer> SpiritHealerSystem::GetPendingResurrect() const {
  return pendingOffer_;
}

bool SpiritHealerSystem::HasPendingResurrect() const {
  return pendingOffer_.has_value();
}

void SpiritHealerSystem::AcceptResurrect() { pendingOffer_.reset(); }

void SpiritHealerSystem::DeclineResurrect() { pendingOffer_.reset(); }

float SpiritHealerSystem::GetResSicknessDuration(float timeDead) const {
  constexpr float kResSicknessThreshold = 360.0f;
  constexpr float kMaxResSickness = 600.0f;

  if (timeDead <= kResSicknessThreshold) return 0.0f;

  float overTime = timeDead - kResSicknessThreshold;
  float duration = std::min(60.0f * std::ceil(overTime / 60.0f), kMaxResSickness);
  return std::max(60.0f, duration);
}

bool SpiritHealerSystem::WillGetResSickness() const {
  return usedSpiritHealer_ || timeDead_ > 360.0f;
}

void SpiritHealerSystem::SetTimeDead(float seconds) { timeDead_ = seconds; }

float SpiritHealerSystem::GetTimeDead() const { return timeDead_; }

ObjectGuid SpiritHealerSystem::GetSpiritHealerGuid() const {
  return spiritHealerGuid_;
}

void SpiritHealerSystem::SetSpiritHealerGuid(ObjectGuid guid) {
  spiritHealerGuid_ = guid;
}

bool SpiritHealerSystem::HasSelfResurrect() const {
  return hasSelfResurrect_;
}

void SpiritHealerSystem::SetSelfResurrect(uint32_t spellId, std::string name) {
  hasSelfResurrect_ = true;
  selfResurrectSpellId_ = spellId;
  selfResurrectName_ = std::move(name);
}

void SpiritHealerSystem::ClearSelfResurrect() {
  hasSelfResurrect_ = false;
  selfResurrectSpellId_ = 0;
  selfResurrectName_.clear();
}

uint32_t SpiritHealerSystem::GetSelfResurrectSpellId() const {
  return selfResurrectSpellId_;
}

void SpiritHealerSystem::Update(float dt) {

  if (pendingOffer_.has_value()) {
    pendingOffer_->timeRemaining -= dt;
    if (pendingOffer_->timeRemaining <= 0.0f) {
      pendingOffer_.reset();
    }
  }
}

void SpiritHealerSystem::Reset() {
  nearSpiritHealer_ = false;
  usedSpiritHealer_ = false;
  pendingOffer_.reset();
  timeDead_ = 0.0f;
  spiritHealerGuid_ = ObjectGuid{};
  hasSelfResurrect_ = false;
  selfResurrectSpellId_ = 0;
  selfResurrectName_.clear();
}

}

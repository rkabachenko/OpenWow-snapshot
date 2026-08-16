
#include "openwow/game/death_system.h"

#include <cmath>

namespace openwow::game {

DeathSystem& DeathSystem::Get() {
  static DeathSystem instance;
  return instance;
}

void DeathSystem::SetDead(bool dead) {
  std::lock_guard lock(mutex_);
  dead_ = dead;
  if (!dead) {
    ghost_ = false;
  }
}

bool DeathSystem::IsDead() const {
  std::lock_guard lock(mutex_);
  return dead_;
}

void DeathSystem::SetGhost(bool ghost) {
  std::lock_guard lock(mutex_);
  ghost_ = ghost;
  if (ghost) {
    dead_ = true;
  }
}

bool DeathSystem::IsGhost() const {
  std::lock_guard lock(mutex_);
  return ghost_;
}

void DeathSystem::SetCorpseLocation(float x, float y, float z,
                                     std::uint32_t map_id) {
  std::lock_guard lock(mutex_);
  corpse_.x = x;
  corpse_.y = y;
  corpse_.z = z;
  corpse_.map_id = map_id;
  corpse_.valid = true;
}

DeathSystem::CorpseLocation DeathSystem::GetCorpseLocation() const {
  std::lock_guard lock(mutex_);
  return corpse_;
}

float DeathSystem::GetDistanceToCorpse(float player_x, float player_y,
                                        float player_z) const {
  std::lock_guard lock(mutex_);
  if (!corpse_.valid) return -1.0f;
  float dx = player_x - corpse_.x;
  float dy = player_y - corpse_.y;
  float dz = player_z - corpse_.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void DeathSystem::SetSpiritHealerAvailable(bool available) {
  std::lock_guard lock(mutex_);
  spirit_healer_ = available;
}

bool DeathSystem::IsSpiritHealerAvailable() const {
  std::lock_guard lock(mutex_);
  return spirit_healer_;
}

void DeathSystem::SetResSickness(std::uint32_t duration) {
  std::lock_guard lock(mutex_);
  res_sickness_duration_ = duration;
}

std::uint32_t DeathSystem::GetResSicknessDuration() const {
  std::lock_guard lock(mutex_);
  return res_sickness_duration_;
}

bool DeathSystem::HasResSickness() const {
  std::lock_guard lock(mutex_);
  return res_sickness_duration_ > 0;
}

void DeathSystem::OfferResurrection(const ObjectGuid& caster,
                                     const std::string& caster_name) {
  std::lock_guard lock(mutex_);
  res_offer_.caster = caster;
  res_offer_.name = caster_name;
  res_offer_.pending = true;
}

void DeathSystem::AcceptResurrection() {
  std::lock_guard lock(mutex_);
  if (!res_offer_.pending) return;
  res_offer_.pending = false;

  dead_ = false;
  ghost_ = false;
}

void DeathSystem::DeclineResurrection() {
  std::lock_guard lock(mutex_);
  res_offer_.pending = false;
}

bool DeathSystem::HasPendingRes() const {
  std::lock_guard lock(mutex_);
  return res_offer_.pending;
}

std::string DeathSystem::GetResCasterName() const {
  std::lock_guard lock(mutex_);
  return res_offer_.name;
}

void DeathSystem::ReleaseSpirit() {
  std::lock_guard lock(mutex_);
  if (!dead_) return;
  ghost_ = true;
  repop_timer_ = 0;
  repop_timer_active_ = false;
}

void DeathSystem::SetRepopTimer(float seconds) {
  std::lock_guard lock(mutex_);
  repop_timer_ = seconds;
  repop_timer_active_ = (seconds > 0);
}

float DeathSystem::GetRepopTimer() const {
  std::lock_guard lock(mutex_);
  return repop_timer_;
}

bool DeathSystem::IsRepopTimerActive() const {
  std::lock_guard lock(mutex_);
  return repop_timer_active_;
}

void DeathSystem::Reset() {
  std::lock_guard lock(mutex_);
  dead_ = false;
  ghost_ = false;
  corpse_ = CorpseLocation{};
  spirit_healer_ = false;
  res_sickness_duration_ = 0;
  res_offer_ = ResOffer{};
  repop_timer_ = 0;
  repop_timer_active_ = false;
}

}

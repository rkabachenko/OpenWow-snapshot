
#include "openwow/game/graveyard_system.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::game {

GraveyardSystem& GraveyardSystem::Get() {
  static GraveyardSystem instance;
  return instance;
}

void GraveyardSystem::AddGraveyard(const GraveyardInfo& info) {
  std::lock_guard lock(mutex_);

  for (auto& g : graveyards_) {
    if (g.graveyardId == info.graveyardId) {
      g = info;
      return;
    }
  }
  graveyards_.push_back(info);
}

void GraveyardSystem::RemoveGraveyard(uint32_t graveyardId) {
  std::lock_guard lock(mutex_);
  graveyards_.erase(
      std::remove_if(graveyards_.begin(), graveyards_.end(),
                     [graveyardId](const GraveyardInfo& g) {
                       return g.graveyardId == graveyardId;
                     }),
      graveyards_.end());
}

std::optional<GraveyardInfo> GraveyardSystem::GetGraveyard(
    uint32_t graveyardId) const {
  std::lock_guard lock(mutex_);
  for (const auto& g : graveyards_) {
    if (g.graveyardId == graveyardId) return g;
  }
  return std::nullopt;
}

uint32_t GraveyardSystem::GetGraveyardCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<uint32_t>(graveyards_.size());
}

std::optional<GraveyardInfo> GraveyardSystem::GetNearestGraveyard(
    float x, float y, float z, uint32_t mapId, uint32_t faction) const {
  std::lock_guard lock(mutex_);
  const GraveyardInfo* best = nullptr;
  float bestDist = std::numeric_limits<float>::max();

  for (const auto& g : graveyards_) {
    if (g.mapId != mapId) continue;

    if (g.factionRequired != 0 && g.factionRequired != faction) continue;

    float dx = g.x - x;
    float dy = g.y - y;
    float dz = g.z - z;
    float dist = dx * dx + dy * dy + dz * dz;
    if (dist < bestDist) {
      bestDist = dist;
      best = &g;
    }
  }

  if (best) return *best;
  return std::nullopt;
}

std::vector<GraveyardInfo> GraveyardSystem::GetGraveyardsForMap(
    uint32_t mapId) const {
  std::lock_guard lock(mutex_);
  std::vector<GraveyardInfo> result;
  for (const auto& g : graveyards_) {
    if (g.mapId == mapId) result.push_back(g);
  }
  return result;
}

bool GraveyardSystem::IsAtGraveyard() const {
  std::lock_guard lock(mutex_);
  return atGraveyard_;
}

void GraveyardSystem::SetAtGraveyard(bool at) {
  std::lock_guard lock(mutex_);
  atGraveyard_ = at;
}

void GraveyardSystem::SetSpiritHealerGuid(ObjectGuid guid) {
  std::lock_guard lock(mutex_);
  spiritHealerGuid_ = guid;
}

ObjectGuid GraveyardSystem::GetSpiritHealerGuid() const {
  std::lock_guard lock(mutex_);
  return spiritHealerGuid_;
}

bool GraveyardSystem::HasSpiritHealer() const {
  std::lock_guard lock(mutex_);
  return !spiritHealerGuid_.IsEmpty();
}

float GraveyardSystem::GetResurrectionTimer() const {
  std::lock_guard lock(mutex_);
  return resurrectionTimer_;
}

void GraveyardSystem::SetResurrectionTimer(float seconds) {
  std::lock_guard lock(mutex_);
  resurrectionTimer_ = seconds;
}

bool GraveyardSystem::ShouldApplyResSickness() const {
  std::lock_guard lock(mutex_);
  return resurrectionTimer_ > 360.0f;
}

float GraveyardSystem::GetCorpseDistance() const {
  std::lock_guard lock(mutex_);
  return corpseDistance_;
}

void GraveyardSystem::SetCorpseDistance(float distance) {
  std::lock_guard lock(mutex_);
  corpseDistance_ = distance;
}

bool GraveyardSystem::IsInGhostForm() const {
  std::lock_guard lock(mutex_);
  return inGhostForm_;
}

void GraveyardSystem::SetInGhostForm(bool ghost) {
  std::lock_guard lock(mutex_);
  inGhostForm_ = ghost;
}

void GraveyardSystem::SetCorpsePosition(float x, float y, float z) {
  std::lock_guard lock(mutex_);
  corpsePos_ = {x, y, z};
}

CorpsePosition GraveyardSystem::GetCorpsePosition() const {
  std::lock_guard lock(mutex_);
  return corpsePos_;
}

void GraveyardSystem::Update(float dt) {
  std::lock_guard lock(mutex_);
  if (inGhostForm_ && dt > 0.0f) {
    resurrectionTimer_ += dt;
  }
}

void GraveyardSystem::Reset() {
  std::lock_guard lock(mutex_);
  graveyards_.clear();
  atGraveyard_ = false;
  spiritHealerGuid_ = ObjectGuid{};
  resurrectionTimer_ = 0.0f;
  corpseDistance_ = 0.0f;
  inGhostForm_ = false;
  corpsePos_ = {};
}

}


#include "openwow/game/bg/bg_arathi_basin.h"

#include <algorithm>

namespace openwow::game {

int BgArathiBasin::GetResourceTickRate(int base_count) {
  if (base_count < 0) return 0;
  if (base_count > 5) base_count = 5;
  return kResourcesPerTick[base_count];
}

bool BgArathiBasin::IsRelevantWorldState(std::int32_t ws_id) {
  if (ws_id == kWsAllianceResources || ws_id == kWsHordeResources)
    return true;

  for (int b = 0; b < 5; ++b) {
    if (ws_id == kBaseWS[b].alliance_controlled ||
        ws_id == kBaseWS[b].horde_controlled ||
        ws_id == kBaseWS[b].alliance_assaulting ||
        ws_id == kBaseWS[b].horde_assaulting) {
      return true;
    }
  }
  return false;
}

void BgArathiBasin::OnWorldStateUpdate(std::int32_t ws_id,
                                        std::int32_t value) {
  if (ws_id == kWsAllianceResources) {
    alliance_resources_ = std::clamp(value, 0, kMaxResources);
    return;
  }
  if (ws_id == kWsHordeResources) {
    horde_resources_ = std::clamp(value, 0, kMaxResources);
    return;
  }

  if (value != 1) return;

  for (int b = 0; b < 5; ++b) {
    if (ws_id == kBaseWS[b].alliance_controlled) {
      bases_[b] = AbBaseState::kAllianceControlled;
      return;
    }
    if (ws_id == kBaseWS[b].horde_controlled) {
      bases_[b] = AbBaseState::kHordeControlled;
      return;
    }
    if (ws_id == kBaseWS[b].alliance_assaulting) {
      bases_[b] = AbBaseState::kAllianceAssaulting;
      return;
    }
    if (ws_id == kBaseWS[b].horde_assaulting) {
      bases_[b] = AbBaseState::kHordeAssaulting;
      return;
    }
  }
}

void BgArathiBasin::Update(float dt) {
  if (remaining_time_ > 0.0f && !IsFinished()) {
    remaining_time_ = std::max(0.0f, remaining_time_ - dt);
  }
}

AbBaseState BgArathiBasin::GetBaseState(AbBase base) const {
  auto idx = static_cast<std::size_t>(base);
  if (idx >= bases_.size()) return AbBaseState::kNeutral;
  return bases_[idx];
}

int BgArathiBasin::GetAllianceBaseCount() const {
  int count = 0;
  for (auto s : bases_) {
    if (s == AbBaseState::kAllianceControlled) ++count;
  }
  return count;
}

int BgArathiBasin::GetHordeBaseCount() const {
  int count = 0;
  for (auto s : bases_) {
    if (s == AbBaseState::kHordeControlled) ++count;
  }
  return count;
}

bool BgArathiBasin::IsFinished() const {
  return alliance_resources_ >= kMaxResources ||
         horde_resources_ >= kMaxResources;
}

std::string_view BgArathiBasin::GetBaseName(AbBase base) {
  switch (base) {
    case AbBase::kStables:    return "Stables";
    case AbBase::kBlacksmith: return "Blacksmith";
    case AbBase::kFarm:       return "Farm";
    case AbBase::kLumberMill: return "Lumber Mill";
    case AbBase::kGoldMine:   return "Gold Mine";
    default:                  return "Unknown";
  }
}

void BgArathiBasin::Reset() {
  bases_.fill(AbBaseState::kNeutral);
  alliance_resources_ = 0;
  horde_resources_ = 0;
  remaining_time_ = kMatchDuration;
}

}

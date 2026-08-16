
#include "openwow/game/bg/bg_alterac_valley.h"

#include <algorithm>

namespace openwow::game {

bool BgAlteracValley::IsRelevantWorldState(std::int32_t ws_id) {
  if (ws_id == kWsAllianceReinforcements ||
      ws_id == kWsHordeReinforcements) {
    return true;
  }

  for (int t = 0; t < 8; ++t) {
    if (ws_id == kTowerWS[t].intact ||
        ws_id == kTowerWS[t].under_assault ||
        ws_id == kTowerWS[t].destroyed) {
      return true;
    }
  }

  for (int g = 0; g < 7; ++g) {
    if (ws_id == kGraveyardWS[g].alliance ||
        ws_id == kGraveyardWS[g].horde ||
        (kGraveyardWS[g].contested != 0 && ws_id == kGraveyardWS[g].contested)) {
      return true;
    }
  }

  return false;
}

void BgAlteracValley::OnWorldStateUpdate(std::int32_t ws_id,
                                          std::int32_t value) {
  if (ws_id == kWsAllianceReinforcements) {
    alliance_reinforcements_ = std::max(0, value);
    return;
  }
  if (ws_id == kWsHordeReinforcements) {
    horde_reinforcements_ = std::max(0, value);
    return;
  }

  if (value == 1) {
    for (int t = 0; t < 8; ++t) {
      if (ws_id == kTowerWS[t].intact) {
        towers_[t] = AvTowerState::kIntact;
        return;
      }
      if (ws_id == kTowerWS[t].under_assault) {
        towers_[t] = AvTowerState::kUnderAssault;
        return;
      }
      if (ws_id == kTowerWS[t].destroyed) {
        towers_[t] = AvTowerState::kDestroyed;
        return;
      }
    }
  }

  if (value == 1) {
    for (int g = 0; g < 7; ++g) {
      if (ws_id == kGraveyardWS[g].alliance) {
        graveyards_[g] = AvGraveyardOwner::kAlliance;
        return;
      }
      if (ws_id == kGraveyardWS[g].horde) {
        graveyards_[g] = AvGraveyardOwner::kHorde;
        return;
      }
      if (kGraveyardWS[g].contested != 0 && ws_id == kGraveyardWS[g].contested) {
        graveyards_[g] = AvGraveyardOwner::kNeutral;
        return;
      }
    }
  }
}

void BgAlteracValley::Update(float ) {

}

AvTowerState BgAlteracValley::GetTowerState(AvTower tower) const {
  auto idx = static_cast<std::size_t>(tower);
  if (idx >= towers_.size()) return AvTowerState::kIntact;
  return towers_[idx];
}

int BgAlteracValley::GetAllianceTowersIntact() const {
  int count = 0;

  for (int i = 0; i < 4; ++i) {
    if (towers_[i] == AvTowerState::kIntact) ++count;
  }
  return count;
}

int BgAlteracValley::GetHordeTowersIntact() const {
  int count = 0;

  for (int i = 4; i < 8; ++i) {
    if (towers_[i] == AvTowerState::kIntact) ++count;
  }
  return count;
}

int BgAlteracValley::GetAllianceTowersDestroyed() const {
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    if (towers_[i] == AvTowerState::kDestroyed) ++count;
  }
  return count;
}

int BgAlteracValley::GetHordeTowersDestroyed() const {
  int count = 0;
  for (int i = 4; i < 8; ++i) {
    if (towers_[i] == AvTowerState::kDestroyed) ++count;
  }
  return count;
}

AvGraveyardOwner BgAlteracValley::GetGraveyardOwner(AvGraveyard gy) const {
  auto idx = static_cast<std::size_t>(gy);
  if (idx >= graveyards_.size()) return AvGraveyardOwner::kNeutral;
  return graveyards_[idx];
}

bool BgAlteracValley::IsBossAlive(bool alliance) const {
  return alliance ? alliance_boss_alive_ : horde_boss_alive_;
}

bool BgAlteracValley::IsFinished() const {
  return alliance_reinforcements_ <= 0 || horde_reinforcements_ <= 0 ||
         !alliance_boss_alive_ || !horde_boss_alive_;
}

std::string_view BgAlteracValley::GetTowerName(AvTower tower) {
  switch (tower) {
    case AvTower::kDunBaldarNorth: return "Dun Baldar North Bunker";
    case AvTower::kDunBaldarSouth: return "Dun Baldar South Bunker";
    case AvTower::kIcewing:        return "Icewing Bunker";
    case AvTower::kStonehearth:    return "Stonehearth Bunker";
    case AvTower::kIcebloodTower:  return "Iceblood Tower";
    case AvTower::kTowerPoint:     return "Tower Point";
    case AvTower::kFrostwolfWest:  return "West Frostwolf Tower";
    case AvTower::kFrostwolfEast:  return "East Frostwolf Tower";
    default:                       return "Unknown Tower";
  }
}

std::string_view BgAlteracValley::GetGraveyardName(AvGraveyard gy) {
  switch (gy) {
    case AvGraveyard::kAidStation:      return "Stormpike Aid Station";
    case AvGraveyard::kStormpikeGY:     return "Stormpike Graveyard";
    case AvGraveyard::kStonehearth:     return "Stonehearth Graveyard";
    case AvGraveyard::kSnowfall:        return "Snowfall Graveyard";
    case AvGraveyard::kIceblood:        return "Iceblood Graveyard";
    case AvGraveyard::kFrostwolfGY:     return "Frostwolf Graveyard";
    case AvGraveyard::kFrostwolfRelief: return "Frostwolf Relief Hut";
    default:                            return "Unknown Graveyard";
  }
}

void BgAlteracValley::Reset() {
  alliance_reinforcements_ = kStartingReinforcements;
  horde_reinforcements_ = kStartingReinforcements;
  towers_.fill(AvTowerState::kIntact);
  graveyards_.fill(AvGraveyardOwner::kNeutral);

  graveyards_[static_cast<int>(AvGraveyard::kAidStation)]      = AvGraveyardOwner::kAlliance;
  graveyards_[static_cast<int>(AvGraveyard::kStormpikeGY)]     = AvGraveyardOwner::kAlliance;
  graveyards_[static_cast<int>(AvGraveyard::kStonehearth)]     = AvGraveyardOwner::kAlliance;
  graveyards_[static_cast<int>(AvGraveyard::kIceblood)]        = AvGraveyardOwner::kHorde;
  graveyards_[static_cast<int>(AvGraveyard::kFrostwolfGY)]     = AvGraveyardOwner::kHorde;
  graveyards_[static_cast<int>(AvGraveyard::kFrostwolfRelief)] = AvGraveyardOwner::kHorde;

  graveyards_[static_cast<int>(AvGraveyard::kSnowfall)]        = AvGraveyardOwner::kNeutral;
  alliance_boss_alive_ = true;
  horde_boss_alive_ = true;
}

}

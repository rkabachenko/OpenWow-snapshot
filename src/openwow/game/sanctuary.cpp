
#include "openwow/game/sanctuary.h"

namespace openwow::game {

void SanctuarySystem::RegisterSanctuary(const SanctuaryZone& zone) {
  sanctuaries_[zone.zoneId] = zone;
}

bool SanctuarySystem::IsSanctuary(std::uint32_t zoneId) const {
  return sanctuaries_.count(zoneId) > 0;
}

std::optional<SanctuaryZone> SanctuarySystem::GetSanctuaryInfo(
    std::uint32_t zoneId) const {
  auto it = sanctuaries_.find(zoneId);
  if (it != sanctuaries_.end()) return it->second;
  return std::nullopt;
}

std::vector<SanctuaryZone> SanctuarySystem::GetAllSanctuaries() const {
  std::vector<SanctuaryZone> result;
  result.reserve(sanctuaries_.size());
  for (const auto& [id, zone] : sanctuaries_) {
    result.push_back(zone);
  }
  return result;
}

bool SanctuarySystem::CanDuelInZone(std::uint32_t zoneId) const {
  auto it = sanctuaries_.find(zoneId);
  if (it != sanctuaries_.end()) return it->second.canDuel;

  return true;
}

bool SanctuarySystem::CanPvPInZone(std::uint32_t zoneId) const {
  if (sanctuaries_.count(zoneId) > 0) return false;
  return true;
}

bool SanctuarySystem::IsMajorCity(std::uint32_t zoneId) const {
  auto it = sanctuaries_.find(zoneId);
  if (it != sanctuaries_.end()) return it->second.isMajorCity;
  return false;
}

bool SanctuarySystem::IsInSanctuary() const {
  return IsSanctuary(current_zone_);
}

void SanctuarySystem::RegisterWotLKSanctuaries() {
  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 4395,
      .name              = "Dalaran",
      .canDuel           = false,
      .canAttackCritters = false,
      .isMajorCity       = true,
  });

  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 3703,
      .name              = "Shattrath City",
      .canDuel           = false,
      .canAttackCritters = false,
      .isMajorCity       = true,
  });

  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 3842,
      .name              = "Area 52",
      .canDuel           = false,
      .canAttackCritters = true,
      .isMajorCity       = false,
  });

  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 3932,
      .name              = "Aldor Rise",
      .canDuel           = false,
      .canAttackCritters = false,
      .isMajorCity       = false,
  });

  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 3933,
      .name              = "The Scryer's Tier",
      .canDuel           = false,
      .canAttackCritters = false,
      .isMajorCity       = false,
  });

  RegisterSanctuary(SanctuaryZone{
      .zoneId            = 4619,
      .name              = "Dalaran Sewers",
      .canDuel           = false,
      .canAttackCritters = false,
      .isMajorCity       = false,
  });
}

void SanctuarySystem::Reset() {
  sanctuaries_.clear();
  current_zone_ = 0;
}

}

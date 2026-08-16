#include "openwow/game/realm_info.h"

#include <algorithm>

namespace openwow::game {

void RealmInfoDisplay::SetRealms(const std::vector<RealmInfoEntry>& realms) {
  realms_ = realms;
}

std::vector<RealmInfoEntry> RealmInfoDisplay::GetRealms() const {
  return realms_;
}

std::optional<RealmInfoEntry> RealmInfoDisplay::GetRealm(
    std::uint32_t realmId) const {
  for (const auto& r : realms_) {
    if (r.realmId == realmId) return r;
  }
  return std::nullopt;
}

std::uint32_t RealmInfoDisplay::GetRealmCount() const {
  return static_cast<std::uint32_t>(realms_.size());
}

std::vector<RealmInfoEntry> RealmInfoDisplay::GetRealmsByType(
    RealmInfoType type) const {
  std::vector<RealmInfoEntry> out;
  for (const auto& r : realms_) {
    if (r.realmType == type) out.push_back(r);
  }
  return out;
}

std::vector<RealmInfoEntry> RealmInfoDisplay::GetRecommendedRealms() const {
  std::vector<RealmInfoEntry> out;
  for (const auto& r : realms_) {
    if (r.isRecommended) out.push_back(r);
  }
  return out;
}

std::vector<RealmInfoEntry> RealmInfoDisplay::GetNewRealms() const {
  std::vector<RealmInfoEntry> out;
  for (const auto& r : realms_) {
    if (r.isNew) out.push_back(r);
  }
  return out;
}

void RealmInfoDisplay::SetSelectedRealm(std::uint32_t realmId) {
  selected_realm_id_ = realmId;
}

std::uint32_t RealmInfoDisplay::GetSelectedRealm() const {
  return selected_realm_id_;
}

bool RealmInfoDisplay::HasSelectedRealm() const {
  return selected_realm_id_ != 0;
}

void RealmInfoDisplay::SortByName() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmInfoEntry& a, const RealmInfoEntry& b) {
              return a.name < b.name;
            });
}

void RealmInfoDisplay::SortByPopulation() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmInfoEntry& a, const RealmInfoEntry& b) {
              return static_cast<int>(a.population) <
                     static_cast<int>(b.population);
            });
}

void RealmInfoDisplay::SortByType() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmInfoEntry& a, const RealmInfoEntry& b) {
              return static_cast<int>(a.realmType) <
                     static_cast<int>(b.realmType);
            });
}

std::string RealmInfoDisplay::GetPopulationName(RealmInfoPopulation pop) {
  switch (pop) {
    case RealmInfoPopulation::Low:    return "Low";
    case RealmInfoPopulation::Medium: return "Medium";
    case RealmInfoPopulation::High:   return "High";
    case RealmInfoPopulation::Full:   return "Full";
    case RealmInfoPopulation::Locked: return "Locked";
  }
  return "Unknown";
}

std::uint32_t RealmInfoDisplay::GetPopulationColor(RealmInfoPopulation pop) {

  switch (pop) {
    case RealmInfoPopulation::Low:    return 0xFF00FF00;
    case RealmInfoPopulation::Medium: return 0xFFFFFF00;
    case RealmInfoPopulation::High:   return 0xFFFFA500;
    case RealmInfoPopulation::Full:   return 0xFFFF0000;
    case RealmInfoPopulation::Locked: return 0xFF808080;
  }
  return 0xFFFFFFFF;
}

std::string RealmInfoDisplay::GetTypeName(RealmInfoType type) {
  switch (type) {
    case RealmInfoType::Normal: return "Normal";
    case RealmInfoType::PvP:    return "PvP";
    case RealmInfoType::RP:     return "RP";
    case RealmInfoType::RPPvP:  return "RP-PvP";
  }
  return "Unknown";
}

void RealmInfoDisplay::Reset() {
  realms_.clear();
  selected_realm_id_ = 0;
}

}

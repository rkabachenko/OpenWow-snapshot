#include "openwow/net/adapters/presentation/realm_list_display.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace openwow::net {

void RealmListDisplay::SetRealms(std::vector<RealmDisplayEntry> realms) {
  realms_ = std::move(realms);
}

const std::vector<RealmDisplayEntry>& RealmListDisplay::GetRealms() const {
  return realms_;
}

std::optional<RealmDisplayEntry> RealmListDisplay::GetRealm(uint32_t id) const {
  for (const auto& r : realms_) {
    if (r.id == id) return r;
  }
  return std::nullopt;
}

size_t RealmListDisplay::GetRealmCount() const { return realms_.size(); }

size_t RealmListDisplay::GetOnlineRealmCount() const {
  return static_cast<size_t>(std::count_if(realms_.begin(), realms_.end(),
      [](const RealmDisplayEntry& r) {
        return (r.flags & RealmDisplayFlag::Offline) == 0;
      }));
}

void RealmListDisplay::SetSelectedRealm(uint32_t id) {
  selected_realm_ = id;
}

std::optional<uint32_t> RealmListDisplay::GetSelectedRealm() const {
  return selected_realm_;
}

void RealmListDisplay::SortByName() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmDisplayEntry& a, const RealmDisplayEntry& b) {
              return a.name < b.name;
            });
}

void RealmListDisplay::SortByPopulation() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmDisplayEntry& a, const RealmDisplayEntry& b) {
              return a.load > b.load;
            });
}

void RealmListDisplay::SortByType() {
  std::sort(realms_.begin(), realms_.end(),
            [](const RealmDisplayEntry& a, const RealmDisplayEntry& b) {
              return static_cast<uint8_t>(a.type) < static_cast<uint8_t>(b.type);
            });
}

std::string RealmListDisplay::GetRealmTypeName(RealmType type) {
  switch (type) {
    case RealmType::Normal: return "Normal";
    case RealmType::PvP:    return "PvP";
    case RealmType::RP:     return "RP";
    case RealmType::RPPvP:  return "RP-PvP";
    default:                return "Unknown";
  }
}

bool RealmListDisplay::IsRealmOnline(uint32_t id) const {
  auto realm = GetRealm(id);
  if (!realm) return false;
  return (realm->flags & RealmDisplayFlag::Offline) == 0;
}

std::optional<RealmDisplayEntry> RealmListDisplay::GetRecommendedRealm() const {
  for (const auto& r : realms_) {
    if (r.flags & RealmDisplayFlag::Recommended) return r;
  }

  for (const auto& r : realms_) {
    if ((r.flags & RealmDisplayFlag::Offline) == 0) return r;
  }
  return std::nullopt;
}

std::string RealmListDisplay::GetPopulationText(float load) {
  if (load >= 2.0f) return "Full";
  if (load >= 1.0f) return "High";
  if (load >= 0.5f) return "Medium";
  return "Low";
}

void RealmListDisplay::Clear() {
  realms_.clear();
  selected_realm_.reset();
}

std::optional<RealmDisplayEntry> RealmListDisplay::FindByName(const std::string& name) const {
  for (const auto& r : realms_) {
    if (r.name.size() != name.size()) continue;
    bool match = true;
    for (std::size_t i = 0; i < r.name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(r.name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        match = false;
        break;
      }
    }
    if (match) return r;
  }
  return std::nullopt;
}

std::optional<RealmDisplayEntry> RealmListDisplay::GetLowestPopulationRealm() const {
  const RealmDisplayEntry* best = nullptr;
  for (const auto& r : realms_) {
    if (r.flags & RealmDisplayFlag::Offline) continue;
    if (!best || r.load < best->load) {
      best = &r;
    }
  }
  return best ? std::optional{*best} : std::nullopt;
}

std::string RealmListDisplay::FormatSummary() const {
  std::ostringstream oss;
  oss << realms_.size() << " realm(s), "
      << GetOnlineRealmCount() << " online";
  if (selected_realm_) {
    auto r = GetRealm(*selected_realm_);
    if (r) {
      oss << ", selected: " << r->name;
    }
  }
  return oss.str();
}

std::vector<std::string> RealmListDisplay::GetRealmNames() const {
  std::vector<std::string> names;
  names.reserve(realms_.size());
  for (const auto& r : realms_) {
    names.push_back(r.name);
  }
  return names;
}

}

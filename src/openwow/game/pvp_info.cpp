
#include "openwow/game/pvp_info.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <string>

namespace openwow::game {

static constexpr std::array<const char*, 14> kAllianceRanks = {{
    "Private",
    "Corporal",
    "Sergeant",
    "Master Sergeant",
    "Sergeant Major",
    "Knight",
    "Knight-Lieutenant",
    "Knight-Captain",
    "Knight-Champion",
    "Lieutenant Commander",
    "Commander",
    "Marshal",
    "Field Marshal",
    "Grand Marshal",
}};

static constexpr std::array<const char*, 14> kHordeRanks = {{
    "Scout",
    "Grunt",
    "Sergeant",
    "Senior Sergeant",
    "First Sergeant",
    "Stone Guard",
    "Blood Guard",
    "Legionnaire",
    "Centurion",
    "Champion",
    "Lieutenant General",
    "General",
    "Warlord",
    "High Warlord",
}};

namespace {

std::string NormalizeLegacyRankTitle(std::string_view raw_title) {
  std::string normalized;
  normalized.reserve(raw_title.size());

  for (std::size_t index = 0; index < raw_title.size(); ++index) {
    if (raw_title[index] == '%' && index + 1 < raw_title.size() &&
        raw_title[index + 1] == 's') {
      ++index;
      continue;
    }

    normalized.push_back(raw_title[index]);
  }

  const auto first = normalized.find_first_not_of(' ');
  if (first == std::string::npos) {
    return {};
  }

  const auto last = normalized.find_last_not_of(' ');
  normalized = normalized.substr(first, last - first + 1);

  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return normalized;
}

std::uint8_t ResolveLegacyRankBadgeIndex(std::string_view title) {
  const std::string normalized = NormalizeLegacyRankTitle(title);
  if (normalized.empty()) {
    return 0;
  }

  for (std::size_t index = 0; index < kAllianceRanks.size(); ++index) {
    if (normalized == NormalizeLegacyRankTitle(kAllianceRanks[index]) ||
        normalized == NormalizeLegacyRankTitle(kHordeRanks[index])) {
      return static_cast<std::uint8_t>(index + 1);
    }
  }

  return 0;
}

}

PvPInfo& PvPInfo::Get() {
  static PvPInfo instance;
  return instance;
}

void PvPInfo::SetHonorPoints(uint32_t points) {
  std::lock_guard lock(mutex_);
  honorPoints_ = points;
}

uint32_t PvPInfo::GetHonorPoints() const {
  std::lock_guard lock(mutex_);
  return honorPoints_;
}

void PvPInfo::SetArenaPoints(uint32_t points) {
  std::lock_guard lock(mutex_);
  arenaPoints_ = points;
}

uint32_t PvPInfo::GetArenaPoints() const {
  std::lock_guard lock(mutex_);
  return arenaPoints_;
}

void PvPInfo::SetTodayHK(uint32_t kills) {
  std::lock_guard lock(mutex_);
  todayHK_ = kills;
}

uint32_t PvPInfo::GetTodayHK() const {
  std::lock_guard lock(mutex_);
  return todayHK_;
}

void PvPInfo::SetTodayHonor(uint32_t honor) {
  std::lock_guard lock(mutex_);
  todayHonor_ = honor;
}

uint32_t PvPInfo::GetTodayHonor() const {
  std::lock_guard lock(mutex_);
  return todayHonor_;
}

void PvPInfo::SetYesterdayHK(uint32_t kills) {
  std::lock_guard lock(mutex_);
  yesterdayHK_ = kills;
}

uint32_t PvPInfo::GetYesterdayHK() const {
  std::lock_guard lock(mutex_);
  return yesterdayHK_;
}

void PvPInfo::SetYesterdayHonor(uint32_t honor) {
  std::lock_guard lock(mutex_);
  yesterdayHonor_ = honor;
}

uint32_t PvPInfo::GetYesterdayHonor() const {
  std::lock_guard lock(mutex_);
  return yesterdayHonor_;
}

void PvPInfo::SetLifetimeHK(uint32_t kills) {
  std::lock_guard lock(mutex_);
  lifetimeHK_ = kills;
}

uint32_t PvPInfo::GetLifetimeHK() const {
  std::lock_guard lock(mutex_);
  return lifetimeHK_;
}

void PvPInfo::SetLifetimeDK(uint32_t deaths) {
  std::lock_guard lock(mutex_);
  lifetimeDK_ = deaths;
}

uint32_t PvPInfo::GetLifetimeDK() const {
  std::lock_guard lock(mutex_);
  return lifetimeDK_;
}

float PvPInfo::GetKDRatio() const {
  std::lock_guard lock(mutex_);
  if (lifetimeDK_ == 0) {
    return (lifetimeHK_ > 0) ? static_cast<float>(lifetimeHK_) : 0.0f;
  }
  return static_cast<float>(lifetimeHK_) / static_cast<float>(lifetimeDK_);
}

void PvPInfo::SetHighestRank(uint32_t rank) {
  std::lock_guard lock(mutex_);
  highestRank_ = rank;
}

uint32_t PvPInfo::GetHighestRank() const {
  std::lock_guard lock(mutex_);
  return highestRank_;
}

std::string PvPInfo::GetRankName(uint32_t rank) const {
  std::lock_guard lock(mutex_);
  if (rank < 1 || rank > 14) return "";
  if (faction_ == 1) {
    return kHordeRanks[rank - 1];
  }
  return kAllianceRanks[rank - 1];
}

void PvPInfo::SetFaction(uint32_t faction) {
  std::lock_guard lock(mutex_);
  faction_ = faction;
}

uint32_t PvPInfo::GetFaction() const {
  std::lock_guard lock(mutex_);
  return faction_;
}

std::vector<std::string> PvPInfo::GetRankNames() const {
  std::lock_guard lock(mutex_);
  const auto& table = (faction_ == 1) ? kHordeRanks : kAllianceRanks;
  return {table.begin(), table.end()};
}

std::uint8_t PvPInfo::ResolveLegacyRankBadgeIndexForTitleTemplate(
    const std::string_view male_title, const std::string_view female_title) {
  if (const auto male_index = ResolveLegacyRankBadgeIndex(male_title);
      male_index != 0) {
    return male_index;
  }

  return ResolveLegacyRankBadgeIndex(female_title);
}

HKCategory PvPInfo::GetHKCategory() const {
  std::lock_guard lock(mutex_);
  return {todayHK_, todayHonor_, yesterdayHK_, yesterdayHonor_,
          lifetimeHK_, lifetimeDK_};
}

void PvPInfo::Reset() {
  std::lock_guard lock(mutex_);
  honorPoints_ = 0;
  arenaPoints_ = 0;
  todayHK_ = 0;
  todayHonor_ = 0;
  yesterdayHK_ = 0;
  yesterdayHonor_ = 0;
  lifetimeHK_ = 0;
  lifetimeDK_ = 0;
  highestRank_ = 0;
  faction_ = 0;
}

}

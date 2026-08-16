
#include "openwow/game/arena_team.h"

#include <algorithm>

namespace openwow::game {

ArenaTeamSystem& ArenaTeamSystem::Get() {
  static ArenaTeamSystem instance;
  return instance;
}

uint8_t ArenaTeamSystem::TypeToIndex(ArenaTeamType type) {
  return static_cast<uint8_t>(type);
}

void ArenaTeamSystem::SetTeam(ArenaTeamType type, const ArenaTeamInfo& info) {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size()) {
    teams_[idx] = info;
    teams_[idx]->teamType = type;
  }
}

std::optional<ArenaTeamInfo> ArenaTeamSystem::GetTeam(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size()) return teams_[idx];
  return std::nullopt;
}

bool ArenaTeamSystem::HasTeam(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  return idx < teams_.size() && teams_[idx].has_value();
}

void ArenaTeamSystem::RemoveTeam(ArenaTeamType type) {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size()) teams_[idx].reset();
}

uint32_t ArenaTeamSystem::GetTeamCount() const {
  std::lock_guard lock(mutex_);
  uint32_t count = 0;
  for (const auto& t : teams_) {
    if (t.has_value()) ++count;
  }
  return count;
}

std::vector<ArenaTeamInfo> ArenaTeamSystem::GetAllTeams() const {
  std::lock_guard lock(mutex_);
  std::vector<ArenaTeamInfo> result;
  for (const auto& t : teams_) {
    if (t.has_value()) result.push_back(*t);
  }
  return result;
}

uint32_t ArenaTeamSystem::GetRating(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) return teams_[idx]->rating;
  return 0;
}

uint32_t ArenaTeamSystem::GetPersonalRating(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) {

    for (const auto& m : teams_[idx]->members) {
      if (m.guid == playerGuid_ && playerGuid_ != 0) {
        return m.personalRating;
      }
    }
  }
  return 0;
}

float ArenaTeamSystem::GetWinRate(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) {
    auto sg = teams_[idx]->seasonGames;
    if (sg == 0) return 0.0f;
    return static_cast<float>(teams_[idx]->seasonWon) /
           static_cast<float>(sg) * 100.0f;
  }
  return 0.0f;
}

std::vector<ArenaRosterMember> ArenaTeamSystem::GetMembers(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) return teams_[idx]->members;
  return {};
}

uint32_t ArenaTeamSystem::GetMemberCount(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) {
    return static_cast<uint32_t>(teams_[idx]->members.size());
  }
  return 0;
}

uint32_t ArenaTeamSystem::GetMaxMembers(ArenaTeamType type) {
  switch (type) {
    case ArenaTeamType::Team2v2: return 4;
    case ArenaTeamType::Team3v3: return 6;
    case ArenaTeamType::Team5v5: return 10;
  }
  return 0;
}

bool ArenaTeamSystem::IsCaptain(ArenaTeamType type) const {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx] && playerGuid_ != 0) {
    return teams_[idx]->captainGuid == playerGuid_;
  }
  return false;
}

void ArenaTeamSystem::SetCaptain(ArenaTeamType type, uint64_t guid) {
  std::lock_guard lock(mutex_);
  auto idx = TypeToIndex(type);
  if (idx < teams_.size() && teams_[idx]) {
    teams_[idx]->captainGuid = guid;
  }
}

void ArenaTeamSystem::SetPlayerGuid(uint64_t guid) {
  std::lock_guard lock(mutex_);
  playerGuid_ = guid;
}

void ArenaTeamSystem::Reset() {
  std::lock_guard lock(mutex_);
  for (auto& t : teams_) t.reset();
  playerGuid_ = 0;
}

}

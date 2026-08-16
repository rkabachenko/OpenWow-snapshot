
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

class ObjectGuid;

enum class ArenaTeamType : uint8_t {
  Team2v2 = 0,
  Team3v3 = 1,
  Team5v5 = 2,
};

inline constexpr uint8_t kArenaTeamTypeCount = 3;

struct ArenaRosterMember {
  uint64_t guid = 0;
  std::string name;
  uint32_t classId = 0;
  uint32_t raceId = 0;
  uint32_t level = 0;
  bool online = false;
  uint32_t weekGames = 0;
  uint32_t weekWon = 0;
  uint32_t seasonGames = 0;
  uint32_t seasonWon = 0;
  uint32_t personalRating = 0;
  float mmrChange = 0.0f;
  float mmrValue = 0.0f;
};

struct ArenaTeamInfo {
  uint32_t teamId = 0;
  std::string teamName;
  ArenaTeamType teamType = ArenaTeamType::Team2v2;
  uint32_t rating = 0;
  uint32_t weekGames = 0;
  uint32_t weekWon = 0;
  uint32_t seasonGames = 0;
  uint32_t seasonWon = 0;
  uint32_t rank = 0;
  uint64_t captainGuid = 0;
  std::vector<ArenaRosterMember> members;
};

class ArenaTeamSystem {
 public:
  static ArenaTeamSystem& Get();

  ArenaTeamSystem(const ArenaTeamSystem&) = delete;
  ArenaTeamSystem& operator=(const ArenaTeamSystem&) = delete;

  void SetTeam(ArenaTeamType type, const ArenaTeamInfo& info);
  [[nodiscard]] std::optional<ArenaTeamInfo> GetTeam(ArenaTeamType type) const;
  [[nodiscard]] bool HasTeam(ArenaTeamType type) const;
  void RemoveTeam(ArenaTeamType type);

  [[nodiscard]] uint32_t GetTeamCount() const;

  [[nodiscard]] std::vector<ArenaTeamInfo> GetAllTeams() const;

  [[nodiscard]] uint32_t GetRating(ArenaTeamType type) const;
  [[nodiscard]] uint32_t GetPersonalRating(ArenaTeamType type) const;
  [[nodiscard]] float GetWinRate(ArenaTeamType type) const;

  [[nodiscard]] std::vector<ArenaRosterMember> GetMembers(ArenaTeamType type) const;
  [[nodiscard]] uint32_t GetMemberCount(ArenaTeamType type) const;

  [[nodiscard]] static uint32_t GetMaxMembers(ArenaTeamType type);

  [[nodiscard]] bool IsCaptain(ArenaTeamType type) const;
  void SetCaptain(ArenaTeamType type, uint64_t guid);

  void SetPlayerGuid(uint64_t guid);

  void Reset();

 private:
  ArenaTeamSystem() = default;

  static uint8_t TypeToIndex(ArenaTeamType type);

  std::array<std::optional<ArenaTeamInfo>, kArenaTeamTypeCount> teams_;
  uint64_t playerGuid_ = 0;
  mutable std::mutex mutex_;
};

}

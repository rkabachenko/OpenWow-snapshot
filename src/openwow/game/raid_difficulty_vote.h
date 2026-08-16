#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

enum class DiffVoteType : std::uint8_t {
  DungeonNormal  = 0,
  DungeonHeroic  = 1,
  Raid10Normal   = 2,
  Raid10Heroic   = 3,
  Raid25Normal   = 4,
  Raid25Heroic   = 5,
};

inline constexpr std::uint8_t kDiffVoteTypeCount = 6;

enum class DiffVoteState : std::uint8_t {
  None     = 0,
  Proposed = 1,
  Voting   = 2,
  Accepted = 3,
  Rejected = 4,
  TimedOut = 5,
};

inline constexpr std::uint8_t kDiffVoteStateCount = 6;

struct DiffVoteMemberInfo {
  std::uint64_t guid{0};
  std::string name;
  bool accepted{false};
  bool responded{false};
};

inline constexpr std::uint32_t kDefaultDiffVoteTimeoutSec = 30;

class RaidDifficultyVote {
 public:

  void ProposeChange(DiffVoteType newDifficulty);

  [[nodiscard]] DiffVoteType GetProposedDifficulty() const;

  [[nodiscard]] DiffVoteState GetState() const;

  [[nodiscard]] DiffVoteType GetCurrentDifficulty() const;
  void SetCurrentDifficulty(DiffVoteType diff);

  void AddMember(const DiffVoteMemberInfo& info);

  bool SetMemberVote(std::uint64_t guid, bool accepted);

  [[nodiscard]] const std::vector<DiffVoteMemberInfo>& GetMembers() const;

  [[nodiscard]] std::uint32_t GetAcceptedCount() const;
  [[nodiscard]] std::uint32_t GetRespondedCount() const;
  [[nodiscard]] std::uint32_t GetTotalMembers() const;

  [[nodiscard]] bool IsUnanimous() const;

  [[nodiscard]] DiffVoteState CheckResult();

  void SetTimeout(std::uint32_t seconds, std::uint32_t elapsedSec);

  [[nodiscard]] bool IsTimedOut() const;

  void Reset();

 private:
  DiffVoteState state_{DiffVoteState::None};
  DiffVoteType currentDifficulty_{DiffVoteType::DungeonNormal};
  DiffVoteType proposedDifficulty_{DiffVoteType::DungeonNormal};
  std::vector<DiffVoteMemberInfo> members_;
  std::uint32_t timeoutSec_{kDefaultDiffVoteTimeoutSec};
  std::uint32_t elapsedSec_{0};
};

}

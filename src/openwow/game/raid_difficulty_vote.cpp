
#include "openwow/game/raid_difficulty_vote.h"

#include <algorithm>

namespace openwow::game {

void RaidDifficultyVote::ProposeChange(DiffVoteType newDifficulty) {
  proposedDifficulty_ = newDifficulty;
  state_ = DiffVoteState::Voting;
  elapsedSec_ = 0;

  for (auto& m : members_) {
    m.accepted = false;
    m.responded = false;
  }
}

DiffVoteType RaidDifficultyVote::GetProposedDifficulty() const {
  return proposedDifficulty_;
}

DiffVoteState RaidDifficultyVote::GetState() const {
  return state_;
}

DiffVoteType RaidDifficultyVote::GetCurrentDifficulty() const {
  return currentDifficulty_;
}

void RaidDifficultyVote::SetCurrentDifficulty(DiffVoteType diff) {
  currentDifficulty_ = diff;
}

void RaidDifficultyVote::AddMember(const DiffVoteMemberInfo& info) {
  members_.push_back(info);
}

bool RaidDifficultyVote::SetMemberVote(std::uint64_t guid, bool accepted) {
  auto it = std::find_if(members_.begin(), members_.end(),
                         [guid](const DiffVoteMemberInfo& m) { return m.guid == guid; });
  if (it == members_.end()) {
    return false;
  }
  it->accepted = accepted;
  it->responded = true;
  return true;
}

const std::vector<DiffVoteMemberInfo>& RaidDifficultyVote::GetMembers() const {
  return members_;
}

std::uint32_t RaidDifficultyVote::GetAcceptedCount() const {
  return static_cast<std::uint32_t>(
      std::count_if(members_.begin(), members_.end(),
                    [](const DiffVoteMemberInfo& m) { return m.accepted; }));
}

std::uint32_t RaidDifficultyVote::GetRespondedCount() const {
  return static_cast<std::uint32_t>(
      std::count_if(members_.begin(), members_.end(),
                    [](const DiffVoteMemberInfo& m) { return m.responded; }));
}

std::uint32_t RaidDifficultyVote::GetTotalMembers() const {
  return static_cast<std::uint32_t>(members_.size());
}

bool RaidDifficultyVote::IsUnanimous() const {
  if (members_.empty()) {
    return false;
  }
  return std::all_of(members_.begin(), members_.end(),
                     [](const DiffVoteMemberInfo& m) { return m.accepted; });
}

DiffVoteState RaidDifficultyVote::CheckResult() {
  if (state_ != DiffVoteState::Voting) {
    return state_;
  }

  if (elapsedSec_ >= timeoutSec_) {
    state_ = DiffVoteState::TimedOut;
    return state_;
  }

  if (GetRespondedCount() == GetTotalMembers() && GetTotalMembers() > 0) {
    if (IsUnanimous()) {
      state_ = DiffVoteState::Accepted;
    } else {
      state_ = DiffVoteState::Rejected;
    }
  }
  return state_;
}

void RaidDifficultyVote::SetTimeout(std::uint32_t seconds, std::uint32_t elapsedSec) {
  timeoutSec_ = seconds;
  elapsedSec_ = elapsedSec;
}

bool RaidDifficultyVote::IsTimedOut() const {
  return elapsedSec_ >= timeoutSec_;
}

void RaidDifficultyVote::Reset() {
  state_ = DiffVoteState::None;
  proposedDifficulty_ = DiffVoteType::DungeonNormal;
  members_.clear();
  elapsedSec_ = 0;
  timeoutSec_ = kDefaultDiffVoteTimeoutSec;
}

}

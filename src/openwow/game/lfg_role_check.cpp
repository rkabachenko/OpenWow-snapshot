
#include "openwow/game/lfg_role_check.h"

#include <algorithm>

namespace openwow::game {

void LFGRoleCheckDisplay::Begin(std::vector<LFGRoleMemberInfo> members,
                                float timeout) {
  members_          = std::move(members);
  timeRemaining_    = timeout;
  active_           = true;
  proposal_.reset();
  proposalAccepted_ = false;
  proposalDeclined_ = false;
}

void LFGRoleCheckDisplay::SetMemberState(ObjectGuid guid,
                                         LFGRoleCheckState state,
                                         std::set<LFGRoleType> roles) {
  for (auto& m : members_) {
    if (m.guid.GetRawValue() == guid.GetRawValue()) {
      m.state         = state;
      m.selectedRoles = std::move(roles);
      return;
    }
  }
}

std::vector<LFGRoleMemberInfo> LFGRoleCheckDisplay::GetMembers() const {
  return members_;
}

std::optional<LFGRoleMemberInfo> LFGRoleCheckDisplay::GetMember(
    ObjectGuid guid) const {
  for (const auto& m : members_)
    if (m.guid.GetRawValue() == guid.GetRawValue()) return m;
  return std::nullopt;
}

bool LFGRoleCheckDisplay::IsComplete() const {
  return !members_.empty() &&
         std::all_of(members_.begin(), members_.end(), [](const auto& m) {
           return m.state != LFGRoleCheckState::Pending;
         });
}

bool LFGRoleCheckDisplay::IsAllAccepted() const {
  return !members_.empty() &&
         std::all_of(members_.begin(), members_.end(), [](const auto& m) {
           return m.state == LFGRoleCheckState::Accepted;
         });
}

float LFGRoleCheckDisplay::GetTimeRemaining() const {
  return timeRemaining_;
}

size_t LFGRoleCheckDisplay::GetAcceptedCount() const {
  return static_cast<size_t>(
      std::count_if(members_.begin(), members_.end(), [](const auto& m) {
        return m.state == LFGRoleCheckState::Accepted;
      }));
}

void LFGRoleCheckDisplay::SetProposal(LFGProposalDisplay proposal) {
  proposal_         = std::move(proposal);
  proposalAccepted_ = false;
  proposalDeclined_ = false;
}

std::optional<LFGProposalDisplay> LFGRoleCheckDisplay::GetProposal() const {
  return proposal_;
}

bool LFGRoleCheckDisplay::HasProposal() const {
  return proposal_.has_value();
}

void LFGRoleCheckDisplay::AcceptProposal() {
  proposalAccepted_ = true;
  proposalDeclined_ = false;
}

void LFGRoleCheckDisplay::DeclineProposal() {
  proposalDeclined_ = true;
  proposalAccepted_ = false;
}

bool LFGRoleCheckDisplay::IsProposalAccepted() const {
  return proposalAccepted_;
}

bool LFGRoleCheckDisplay::IsProposalDeclined() const {
  return proposalDeclined_;
}

void LFGRoleCheckDisplay::Update(float dt) {
  if (!active_) return;
  timeRemaining_ -= dt;
  if (timeRemaining_ < 0.0f) timeRemaining_ = 0.0f;

  if (timeRemaining_ <= 0.0f) {
    for (auto& m : members_) {
      if (m.state == LFGRoleCheckState::Pending)
        m.state = LFGRoleCheckState::TimedOut;
    }
  }

  if (proposal_ && proposal_->timeRemaining > 0.0f) {
    proposal_->timeRemaining -= dt;
    if (proposal_->timeRemaining < 0.0f)
      proposal_->timeRemaining = 0.0f;
  }
}

bool LFGRoleCheckDisplay::IsActive() const { return active_; }

void LFGRoleCheckDisplay::Cancel() {
  active_ = false;
}

void LFGRoleCheckDisplay::Reset() {
  members_.clear();
  timeRemaining_    = 0.0f;
  active_           = false;
  proposal_.reset();
  proposalAccepted_ = false;
  proposalDeclined_ = false;
}

}

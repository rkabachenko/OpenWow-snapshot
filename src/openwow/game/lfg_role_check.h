#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class LFGRoleType : uint8_t {
  Tank   = 0,
  Healer = 1,
  Damage = 2,
};

enum class LFGRoleCheckState : uint8_t {
  Pending  = 0,
  Accepted = 1,
  Declined = 2,
  TimedOut = 3,
};

struct LFGRoleMemberInfo {
  ObjectGuid              guid;
  std::string             name;
  uint8_t                 classId = 0;
  LFGRoleCheckState       state   = LFGRoleCheckState::Pending;
  std::set<LFGRoleType>   selectedRoles;
};

struct LFGProposalDisplay {
  uint32_t    proposalId    = 0;
  std::string dungeonName;
  uint32_t    dungeonId     = 0;
  float       timeRemaining = 0.0f;
};

class LFGRoleCheckDisplay {
 public:
  void Begin(std::vector<LFGRoleMemberInfo> members, float timeout);

  void SetMemberState(ObjectGuid guid, LFGRoleCheckState state,
                      std::set<LFGRoleType> roles);

  [[nodiscard]] std::vector<LFGRoleMemberInfo> GetMembers() const;
  [[nodiscard]] std::optional<LFGRoleMemberInfo> GetMember(ObjectGuid guid) const;

  [[nodiscard]] bool IsComplete() const;
  [[nodiscard]] bool IsAllAccepted() const;
  [[nodiscard]] float GetTimeRemaining() const;
  [[nodiscard]] size_t GetAcceptedCount() const;

  void SetProposal(LFGProposalDisplay proposal);
  [[nodiscard]] std::optional<LFGProposalDisplay> GetProposal() const;
  [[nodiscard]] bool HasProposal() const;

  void AcceptProposal();
  void DeclineProposal();
  [[nodiscard]] bool IsProposalAccepted() const;
  [[nodiscard]] bool IsProposalDeclined() const;

  void Update(float dt);

  [[nodiscard]] bool IsActive() const;
  void Cancel();

  void Reset();

 private:
  std::vector<LFGRoleMemberInfo> members_;
  float                          timeRemaining_ = 0.0f;
  bool                           active_        = false;

  std::optional<LFGProposalDisplay> proposal_;
  bool                              proposalAccepted_ = false;
  bool                              proposalDeclined_ = false;
};

}

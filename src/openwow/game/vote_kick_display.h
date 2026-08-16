#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <optional>
#include <string>

namespace openwow::game {

enum class VoteKickDisplayState : uint8_t {
    Idle         = 0,
    PendingVote  = 1,
    VoteInProgress = 2,
    VoteComplete = 3,
};

struct VoteKickInfo {
    ObjectGuid targetGuid;
    std::string targetName;
    std::string reason;
    float timeRemaining = 0.0f;
    uint8_t votesFor = 0;
    uint8_t votesAgainst = 0;
    uint8_t totalVoters = 0;
    VoteKickDisplayState state = VoteKickDisplayState::Idle;
    bool hasVoted = false;
    bool myVote = false;
};

class VoteKickDisplay {
 public:
    void StartVote(ObjectGuid target, const std::string& name,
                   const std::string& reason, uint8_t totalVoters,
                   float timeout);
    bool CastVote(bool inFavor);

    [[nodiscard]] std::optional<VoteKickInfo> GetInfo() const;
    [[nodiscard]] VoteKickDisplayState GetState() const;
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] bool HasVoted() const;
    [[nodiscard]] float GetTimeRemaining() const;
    [[nodiscard]] std::string GetResultText() const;
    [[nodiscard]] uint8_t GetRequiredVotes() const;
    [[nodiscard]] bool IsPassed() const;
    [[nodiscard]] bool IsFailed() const;

    void Update(float dt);
    void Reset();

 private:
    std::optional<VoteKickInfo> info_;
};

}

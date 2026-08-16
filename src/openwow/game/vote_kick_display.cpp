
#include "openwow/game/vote_kick_display.h"

namespace openwow::game {

void VoteKickDisplay::StartVote(ObjectGuid target, const std::string& name,
                                const std::string& reason,
                                uint8_t totalVoters, float timeout) {
    VoteKickInfo info;
    info.targetGuid = target;
    info.targetName = name;
    info.reason = reason;
    info.totalVoters = totalVoters;
    info.timeRemaining = timeout;
    info.state = VoteKickDisplayState::VoteInProgress;
    info.votesFor = 0;
    info.votesAgainst = 0;
    info.hasVoted = false;
    info.myVote = false;
    info_ = info;
}

bool VoteKickDisplay::CastVote(bool inFavor) {
    if (!info_ || info_->state != VoteKickDisplayState::VoteInProgress)
        return false;
    if (info_->hasVoted) return false;

    info_->hasVoted = true;
    info_->myVote = inFavor;
    if (inFavor)
        ++info_->votesFor;
    else
        ++info_->votesAgainst;

    const uint8_t required = GetRequiredVotes();
    if (info_->votesFor >= required || info_->votesAgainst >= required ||
        (info_->votesFor + info_->votesAgainst) >= info_->totalVoters) {
        info_->state = VoteKickDisplayState::VoteComplete;
    }

    return true;
}

std::optional<VoteKickInfo> VoteKickDisplay::GetInfo() const {
    return info_;
}

VoteKickDisplayState VoteKickDisplay::GetState() const {
    if (!info_) return VoteKickDisplayState::Idle;
    return info_->state;
}

bool VoteKickDisplay::IsActive() const {
    return info_.has_value() &&
           info_->state == VoteKickDisplayState::VoteInProgress;
}

bool VoteKickDisplay::HasVoted() const {
    if (!info_) return false;
    return info_->hasVoted;
}

float VoteKickDisplay::GetTimeRemaining() const {
    if (!info_) return 0.0f;
    return info_->timeRemaining;
}

std::string VoteKickDisplay::GetResultText() const {
    if (!info_) return "";

    switch (info_->state) {
        case VoteKickDisplayState::VoteInProgress:
            return "Voting...";
        case VoteKickDisplayState::VoteComplete:
            return IsPassed() ? "Vote passed" : "Vote failed";
        case VoteKickDisplayState::PendingVote:
            return "Pending...";
        case VoteKickDisplayState::Idle:
        default:
            return "";
    }
}

uint8_t VoteKickDisplay::GetRequiredVotes() const {
    if (!info_) return 0;

    return static_cast<uint8_t>((info_->totalVoters / 2) + 1);
}

bool VoteKickDisplay::IsPassed() const {
    if (!info_ || info_->state != VoteKickDisplayState::VoteComplete)
        return false;
    return info_->votesFor >= GetRequiredVotes();
}

bool VoteKickDisplay::IsFailed() const {
    if (!info_ || info_->state != VoteKickDisplayState::VoteComplete)
        return false;
    return !IsPassed();
}

void VoteKickDisplay::Update(float dt) {
    if (!info_) return;
    if (info_->state != VoteKickDisplayState::VoteInProgress) return;

    info_->timeRemaining -= dt;
    if (info_->timeRemaining <= 0.0f) {
        info_->timeRemaining = 0.0f;
        info_->state = VoteKickDisplayState::VoteComplete;
    }
}

void VoteKickDisplay::Reset() {
    info_.reset();
}

}

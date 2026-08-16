
#include "openwow/game/lfg_queue_status.h"

#include <cmath>
#include <cstdio>

namespace openwow::game {

void LFGQueueStatusDisplay::SetStatus(const LFGQueueStatusInfo& info) {
    status_ = info;
}

LFGQueueStatusInfo LFGQueueStatusDisplay::GetStatus() const {
    return status_;
}

LFGQueueState LFGQueueStatusDisplay::GetState() const {
    return status_.state;
}

bool LFGQueueStatusDisplay::IsQueued() const {
    return status_.state == LFGQueueState::Queued ||
           status_.state == LFGQueueState::Proposal;
}

std::string LFGQueueStatusDisplay::FormatTime(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    auto totalSec = static_cast<int>(std::floor(seconds));
    int hours = totalSec / 3600;
    int minutes = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    char buf[32];
    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, secs);
    }
    return buf;
}

std::string LFGQueueStatusDisplay::GetQueuedTimeFormatted() const {
    return FormatTime(status_.queuedTime);
}

std::string LFGQueueStatusDisplay::GetEstimatedWaitFormatted() const {
    return FormatTime(status_.estimatedWait);
}

void LFGQueueStatusDisplay::Update(float deltaTime) {
    if (status_.state == LFGQueueState::Queued) {
        status_.queuedTime += deltaTime;
    } else if (status_.state == LFGQueueState::Proposal) {
        status_.queuedTime += deltaTime;
        status_.proposalElapsed += deltaTime;
    }
}

void LFGQueueStatusDisplay::SetRoleCounts(const LFGQueueRoleCount& roles) {
    status_.roles = roles;
}

bool LFGQueueStatusDisplay::GetRoleFilled(LFGQueueRoleType role) const {
    switch (role) {
        case LFGQueueRoleType::Tank:
            return status_.roles.tanksCurrent >= status_.roles.tanksNeeded;
        case LFGQueueRoleType::Healer:
            return status_.roles.healersCurrent >= status_.roles.healersNeeded;
        case LFGQueueRoleType::DPS:
            return status_.roles.dpsCurrent >= status_.roles.dpsNeeded;
    }
    return false;
}

bool LFGQueueStatusDisplay::AreAllRolesFilled() const {
    return GetRoleFilled(LFGQueueRoleType::Tank) &&
           GetRoleFilled(LFGQueueRoleType::Healer) &&
           GetRoleFilled(LFGQueueRoleType::DPS);
}

void LFGQueueStatusDisplay::LeaveQueue() {
    status_.state = LFGQueueState::None;
    status_.queuedTime = 0.0f;
    status_.proposalAccepted = false;
    status_.proposalElapsed = 0.0f;
}

LFGQueueRoleType LFGQueueStatusDisplay::GetMyRole() const {
    return status_.myRole;
}

std::string LFGQueueStatusDisplay::GetDungeonName() const {
    return status_.dungeonName;
}

void LFGQueueStatusDisplay::SetProposalAccepted(bool accepted) {
    if (status_.state == LFGQueueState::Proposal) {
        status_.proposalAccepted = accepted;
    }
}

bool LFGQueueStatusDisplay::IsProposalReady() const {
    return status_.state == LFGQueueState::Proposal && AreAllRolesFilled();
}

void LFGQueueStatusDisplay::UpdateProposal(float deltaTime) {
    if (status_.state != LFGQueueState::Proposal) return;
    status_.proposalElapsed += deltaTime;
}

float LFGQueueStatusDisplay::GetProposalTimeRemaining() const {
    if (status_.state != LFGQueueState::Proposal) return 0.0f;
    float remaining = status_.proposalTimeout - status_.proposalElapsed;
    return remaining > 0.0f ? remaining : 0.0f;
}

float LFGQueueStatusDisplay::GetProposalProgress() const {
    if (status_.state != LFGQueueState::Proposal || status_.proposalTimeout <= 0.0f)
        return 0.0f;
    float progress = status_.proposalElapsed / status_.proposalTimeout;
    return progress > 1.0f ? 1.0f : progress;
}

bool LFGQueueStatusDisplay::IsProposalExpired() const {
    return status_.state == LFGQueueState::Proposal &&
           status_.proposalElapsed >= status_.proposalTimeout;
}

LFGWaitCategory LFGQueueStatusDisplay::ComputeWaitCategory(float estimatedSeconds) {
    if (estimatedSeconds <= 0.0f) return LFGWaitCategory::Unavailable;
    if (estimatedSeconds < 300.0f) return LFGWaitCategory::Short;
    if (estimatedSeconds < 600.0f) return LFGWaitCategory::Medium;
    if (estimatedSeconds < 1800.0f) return LFGWaitCategory::Long;
    return LFGWaitCategory::Unavailable;
}

std::string LFGQueueStatusDisplay::GetWaitCategoryText() const {
    switch (ComputeWaitCategory(status_.estimatedWait)) {
        case LFGWaitCategory::Short:       return "Short";
        case LFGWaitCategory::Medium:      return "Medium";
        case LFGWaitCategory::Long:        return "Long";
        case LFGWaitCategory::Unavailable: return "Unavailable";
    }
    return "Unavailable";
}

std::string LFGQueueStatusDisplay::GetTankWaitFormatted() const {
    return FormatTime(status_.tankWait);
}

std::string LFGQueueStatusDisplay::GetHealerWaitFormatted() const {
    return FormatTime(status_.healerWait);
}

std::string LFGQueueStatusDisplay::GetDPSWaitFormatted() const {
    return FormatTime(status_.dpsWait);
}

float LFGQueueStatusDisplay::GetAverageRoleWait() const {

    float sum = 0.0f;
    int count = 0;
    if (status_.tankWait > 0.0f) { sum += status_.tankWait; ++count; }
    if (status_.healerWait > 0.0f) { sum += status_.healerWait; ++count; }
    if (status_.dpsWait > 0.0f) { sum += status_.dpsWait; ++count; }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

void LFGQueueStatusDisplay::Suspend() {
    if (status_.state == LFGQueueState::Queued) {
        status_.state = LFGQueueState::Suspended;
    }
}

void LFGQueueStatusDisplay::Resume() {
    if (status_.state == LFGQueueState::Suspended) {
        status_.state = LFGQueueState::Queued;
    }
}

std::string LFGQueueStatusDisplay::GetStatusSummary() const {
    switch (status_.state) {
        case LFGQueueState::None:
            return "Not in queue";
        case LFGQueueState::Queued: {
            std::string summary = "In Queue: " + status_.dungeonName;
            summary += "\nWait: " + GetQueuedTimeFormatted();
            summary += " (Est. " + GetWaitCategoryText() + ")";

            if (status_.roles.tanksNeeded > 0 &&
                status_.roles.tanksCurrent < status_.roles.tanksNeeded) {
                summary += "\nTank needed";
            }
            if (status_.roles.healersNeeded > 0 &&
                status_.roles.healersCurrent < status_.roles.healersNeeded) {
                summary += "\nHealer needed";
            }
            if (status_.roles.dpsNeeded > 0 &&
                status_.roles.dpsCurrent < status_.roles.dpsNeeded) {
                int needed = status_.roles.dpsNeeded - status_.roles.dpsCurrent;
                summary += "\n" + std::to_string(needed) + " DPS needed";
            }
            return summary;
        }
        case LFGQueueState::Proposal:
            return "Dungeon Ready: " + status_.dungeonName;
        case LFGQueueState::InDungeon:
            return "In Dungeon: " + status_.dungeonName;
        case LFGQueueState::Suspended:
            return "Queue Suspended: " + status_.dungeonName;
    }
    return "Unknown";
}

}

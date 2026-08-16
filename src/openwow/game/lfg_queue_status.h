
#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class LFGQueueRoleType : std::uint8_t {
    Tank = 0,
    Healer = 1,
    DPS = 2,
};

enum class LFGQueueState : std::uint8_t {
    None = 0,
    Queued = 1,
    Proposal = 2,
    InDungeon = 3,
    Suspended = 4,
};

enum class LFGWaitCategory : std::uint8_t {
    Unavailable = 0,
    Long        = 1,
    Medium      = 2,
    Short       = 3,
};

struct LFGQueueRoleCount {
    std::uint8_t tanksCurrent{0};
    std::uint8_t tanksNeeded{1};
    std::uint8_t healersCurrent{0};
    std::uint8_t healersNeeded{1};
    std::uint8_t dpsCurrent{0};
    std::uint8_t dpsNeeded{3};
};

struct LFGQueueStatusInfo {
    LFGQueueState state{LFGQueueState::None};
    std::string dungeonName;
    std::uint32_t dungeonId{0};
    float queuedTime{0.0f};
    float estimatedWait{0.0f};
    float averageWait{0.0f};
    float tankWait{0.0f};
    float healerWait{0.0f};
    float dpsWait{0.0f};
    LFGQueueRoleCount roles;
    LFGQueueRoleType myRole{LFGQueueRoleType::DPS};
    bool isLeader{false};
    bool proposalAccepted{false};
    float proposalTimeout{40.0f};
    float proposalElapsed{0.0f};

    std::uint32_t rewardXP{0};
    std::uint32_t rewardMoney{0};
    std::uint32_t rewardItemId{0};
    std::uint32_t rewardItemCount{0};
    bool isFirstCompletion{true};
};

class LFGQueueStatusDisplay {
 public:

    void SetStatus(const LFGQueueStatusInfo& info);

    [[nodiscard]] LFGQueueStatusInfo GetStatus() const;

    [[nodiscard]] LFGQueueState GetState() const;

    [[nodiscard]] bool IsQueued() const;

    [[nodiscard]] std::string GetQueuedTimeFormatted() const;

    [[nodiscard]] std::string GetEstimatedWaitFormatted() const;

    void Update(float deltaTime);

    void SetRoleCounts(const LFGQueueRoleCount& roles);

    [[nodiscard]] bool GetRoleFilled(LFGQueueRoleType role) const;

    [[nodiscard]] bool AreAllRolesFilled() const;

    void LeaveQueue();

    [[nodiscard]] LFGQueueRoleType GetMyRole() const;

    [[nodiscard]] std::string GetDungeonName() const;

    void SetProposalAccepted(bool accepted);

    [[nodiscard]] bool IsProposalReady() const;

    void UpdateProposal(float deltaTime);

    [[nodiscard]] float GetProposalTimeRemaining() const;

    [[nodiscard]] float GetProposalProgress() const;

    [[nodiscard]] bool IsProposalExpired() const;

    [[nodiscard]] static LFGWaitCategory ComputeWaitCategory(float estimatedSeconds);

    [[nodiscard]] std::string GetWaitCategoryText() const;

    [[nodiscard]] std::string GetTankWaitFormatted() const;
    [[nodiscard]] std::string GetHealerWaitFormatted() const;
    [[nodiscard]] std::string GetDPSWaitFormatted() const;

    [[nodiscard]] float GetAverageRoleWait() const;

    void Suspend();

    void Resume();

    [[nodiscard]] std::string GetStatusSummary() const;

 private:
    [[nodiscard]] static std::string FormatTime(float seconds);

    LFGQueueStatusInfo status_;
};

}

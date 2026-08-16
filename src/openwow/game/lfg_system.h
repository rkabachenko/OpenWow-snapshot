
#pragma once

#include <chrono>
#include <cstdint>
#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/runtime/scheduling/burst_throttle.h"
#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class LFGRoleFlag : uint32_t {
    Tank   = 2,
    Healer = 4,
    DPS    = 8,
};

struct LFGQueueInfo {
    uint32_t averageWait  = 0;
    uint32_t myWait       = 0;
    uint32_t tankWait     = 0;
    uint32_t healerWait   = 0;
    uint32_t dpsWait      = 0;
    uint32_t tankCount    = 0;
    uint32_t healerCount  = 0;
    uint32_t dpsCount     = 0;
    uint32_t tankNeeded   = 0;
    uint32_t healerNeeded = 0;
    uint32_t dpsNeeded    = 0;
};

enum class LFGState : uint8_t {
    None = 0,
    Queued = 1,
    Proposal = 2,
    Boot = 3,
    InDungeon = 4,
};

struct LFGProposal {
    uint32_t id = 0;
    uint32_t dungeon_id = 0;
    uint32_t encounter_completion_mask = 0;
    uint8_t state = 0;
    bool silent = false;
    struct ProposalMember {
        uint64_t guid = 0;
        uint8_t role = 0;

        std::optional<bool> accepted;

        bool self = false;
        bool in_dungeon = false;
    };
    std::vector<ProposalMember> members;
};

struct LFGReward {
    uint32_t dungeon_id = 0;
    bool is_first_reward = false;
    uint32_t reward_money = 0;
    uint32_t reward_xp = 0;
    std::vector<uint32_t> reward_item_ids;
    std::vector<uint32_t> reward_item_counts;
};

struct LFGBootVote {
    bool in_progress = false;
    uint64_t target_guid = 0;
    std::string reason;
    uint32_t agree_count = 0;
    uint32_t time_left = 0;
    uint32_t votes_needed = 3;
    uint32_t votes_total = 0;
    bool did_vote = false;
    bool my_vote = false;
};

class LFGSystem {
 public:
    using CommentThrottleClock = std::chrono::steady_clock;
    using JoinThrottleClock = std::chrono::steady_clock;

    static LFGSystem& Get();

    void SetState(LFGState state);
    [[nodiscard]] LFGState GetState() const;
    [[nodiscard]] bool IsQueued() const;

    void SetQueueInfo(uint32_t dungeonId, uint32_t avgWaitTime,
                      uint32_t waitTime, uint32_t timeInQueue, bool queued);
    [[nodiscard]] uint32_t GetAverageWaitTime() const;
    [[nodiscard]] uint32_t GetTimeInQueue() const;

    void SetProposal(const LFGProposal& proposal);
    [[nodiscard]] const LFGProposal* GetProposal() const;
    [[nodiscard]] bool HasProposal() const;
    void ApplyProposalResponse(bool accept);
    void ClearProposal();

    void SetRoles(uint8_t roles);
    [[nodiscard]] uint8_t GetRoles() const;
    [[nodiscard]] static uint8_t GetRoleAvailabilityMaskForClass(uint8_t class_id);
    [[nodiscard]] static uint8_t FilterRolesForClass(uint8_t class_id,
                                                     uint8_t roles);

    void SetSelectedDungeons(const std::vector<uint32_t>& dungeons);
    [[nodiscard]] const std::vector<uint32_t>& GetSelectedDungeons() const;
    void AddSelectedDungeon(std::uint32_t packed_dungeon_id,
                            std::uint32_t tracked_party_member_count);
    void RemoveSelectedDungeon(std::uint32_t packed_dungeon_id);
    void ClearSelectedDungeons();
    [[nodiscard]] bool ContainsSelectedDungeon(std::uint32_t packed_dungeon_id) const;

    void SetRewards(const std::vector<LFGReward>& rewards);
    [[nodiscard]] size_t GetNumRewards() const;
    [[nodiscard]] const LFGReward* GetReward(size_t index) const;

    void SetBootVote(const LFGBootVote& vote);
    [[nodiscard]] const LFGBootVote* GetBootVote() const;
    [[nodiscard]] std::optional<LFGBootVote> GetBootVoteSnapshot() const;
    [[nodiscard]] bool HasBootVote() const;

    void Reset();

    void JoinQueue(const std::vector<uint32_t>& dungeonIds, uint32_t roles);
    void LeaveQueue();
    [[nodiscard]] uint32_t GetSelectedRoles() const;
    [[nodiscard]] bool HasRole(LFGRoleFlag flag) const;
    [[nodiscard]] bool HasActiveJoinRequest() const;
    [[nodiscard]] bool TryBeginJoinRequest();
    [[nodiscard]] bool TryBeginJoinRequest(JoinThrottleClock::time_point now);
    [[nodiscard]] bool TryPrepareTeleportSend();
    [[nodiscard]] bool TryPrepareTeleportSend(double now_seconds);

    void SetQueueInfo(const LFGQueueInfo& info);
    [[nodiscard]] LFGQueueInfo GetQueueInfo() const;
    [[nodiscard]] uint32_t GetWaitTime() const;

    void SetComment(const std::string& comment);
    [[nodiscard]] std::string GetComment() const;
    [[nodiscard]] bool TryConsumeCommentSendThrottleToken();
    [[nodiscard]] bool TryConsumeCommentSendThrottleToken(
        CommentThrottleClock::time_point now);
    [[nodiscard]] bool TryPrepareBootVoteSend(bool agree);
    [[nodiscard]] bool TryPrepareBootVoteSend(bool agree,
        CommentThrottleClock::time_point now);

    void SetBootVoteInProgress(bool inProgress);
    [[nodiscard]] bool IsBootVoteInProgress() const;
    void VoteToBoot(bool agree);
    void SetBootTarget(ObjectGuid target);
    [[nodiscard]] ObjectGuid GetBootTarget() const;
    [[nodiscard]] uint32_t GetBootVotesNeeded() const;
    void SetBootVoteCount(uint32_t yes, uint32_t no);
    [[nodiscard]] uint32_t GetBootVoteYes() const;
    [[nodiscard]] uint32_t GetBootVoteNo() const;

 private:
    LFGSystem() = default;

    LFGState state_ = LFGState::None;
    uint32_t avg_wait_ = 0;
    uint32_t wait_time_ = 0;
    uint32_t time_in_queue_ = 0;
    std::optional<LFGProposal> proposal_;
    uint8_t roles_ = 0;
    std::vector<uint32_t> selected_dungeons_;
    std::vector<LFGReward> rewards_;
    std::optional<LFGBootVote> boot_vote_;
    mutable std::mutex mutex_;

    LFGQueueInfo queue_info_ext_{};
    uint32_t selected_roles_ = 0;
    std::string comment_;
    CommentThrottleClock::time_point comment_send_window_anchor_{};
    uint32_t comment_send_attempts_ = 0;
    JoinThrottleClock::time_point join_send_window_anchor_{};
    uint32_t join_send_attempts_ = 0;
    openwow::core::IdaBurstThrottle teleport_send_throttle_{};
    bool join_request_active_ = false;
    bool join_request_dirty_ = false;
    CommentThrottleClock::time_point boot_vote_send_window_anchor_{};
    uint32_t boot_vote_send_attempts_ = 0;
};

}

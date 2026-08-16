
#include "openwow/game/lfg_system.h"

#include "openwow/runtime/time/game_clock.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace openwow::game {

namespace {

constexpr std::array<std::uint8_t, 13> kRoleAvailabilityMaskByClassId = {
    0x00, 0x0B, 0x0F, 0x09, 0x09, 0x0D, 0x0B,
    0x0D, 0x09, 0x09, 0x00, 0x0F, 0x00,
};

std::uint8_t LfgSelectionType(const std::uint32_t packed_dungeon_id) {
    return static_cast<std::uint8_t>(packed_dungeon_id >> 24);
}

bool CanPreserveSelectionType(const std::uint8_t new_type,
                              const std::uint32_t tracked_party_member_count,
                              const std::uint32_t existing_packed_dungeon_id) {
    const auto existing_type = LfgSelectionType(existing_packed_dungeon_id);
    if (new_type == 1u || new_type == 5u) {
        return existing_type == 1u || existing_type == 5u;
    }

    if (new_type == 2u && tracked_party_member_count == 0u) {
        return existing_type == 2u;
    }

    return false;
}

}

LFGSystem& LFGSystem::Get() {
    static LFGSystem instance;
    return instance;
}

void LFGSystem::SetState(LFGState state) {
    std::lock_guard lock(mutex_);
    state_ = state;
}

LFGState LFGSystem::GetState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

bool LFGSystem::IsQueued() const {
    std::lock_guard lock(mutex_);
    return state_ == LFGState::Queued;
}

void LFGSystem::SetQueueInfo(uint32_t , uint32_t avgWaitTime,
                             uint32_t waitTime, uint32_t timeInQueue,
                             bool queued) {
    std::lock_guard lock(mutex_);
    avg_wait_ = avgWaitTime;
    wait_time_ = waitTime;
    time_in_queue_ = timeInQueue;
    if (queued && state_ == LFGState::None) {
        state_ = LFGState::Queued;
    }
}

uint32_t LFGSystem::GetAverageWaitTime() const {
    std::lock_guard lock(mutex_);
    return avg_wait_;
}

uint32_t LFGSystem::GetTimeInQueue() const {
    std::lock_guard lock(mutex_);
    return time_in_queue_;
}

void LFGSystem::SetProposal(const LFGProposal& proposal) {
    std::lock_guard lock(mutex_);
    proposal_ = proposal;
}

const LFGProposal* LFGSystem::GetProposal() const {
    std::lock_guard lock(mutex_);
    if (proposal_.has_value()) return &proposal_.value();
    return nullptr;
}

bool LFGSystem::HasProposal() const {
    std::lock_guard lock(mutex_);
    return proposal_.has_value();
}

void LFGSystem::ApplyProposalResponse(bool accept) {
    std::lock_guard lock(mutex_);
    if (!proposal_.has_value()) {
        return;
    }

    if (!accept) {
        proposal_.reset();
        return;
    }

    for (auto& member : proposal_->members) {
        if (!member.self) {
            continue;
    }

    member.accepted = true;
  }
}

void LFGSystem::ClearProposal() {
    std::lock_guard lock(mutex_);
    proposal_.reset();
}

void LFGSystem::SetRoles(uint8_t roles) {
    std::lock_guard lock(mutex_);
    roles_ = roles;
}

uint8_t LFGSystem::GetRoles() const {
    std::lock_guard lock(mutex_);
    return roles_;
}

uint8_t LFGSystem::GetRoleAvailabilityMaskForClass(uint8_t class_id) {
    if (class_id >= kRoleAvailabilityMaskByClassId.size()) {
        return 0;
    }
    return kRoleAvailabilityMaskByClassId[class_id];
}

uint8_t LFGSystem::FilterRolesForClass(uint8_t class_id, uint8_t roles) {
    return static_cast<uint8_t>(roles & GetRoleAvailabilityMaskForClass(class_id));
}

void LFGSystem::SetSelectedDungeons(const std::vector<uint32_t>& dungeons) {
    std::lock_guard lock(mutex_);
    if (selected_dungeons_ != dungeons) {
        join_request_dirty_ = true;
    }
    selected_dungeons_ = dungeons;
}

const std::vector<uint32_t>& LFGSystem::GetSelectedDungeons() const {
    std::lock_guard lock(mutex_);
    return selected_dungeons_;
}

void LFGSystem::AddSelectedDungeon(const std::uint32_t packed_dungeon_id,
                                   const std::uint32_t tracked_party_member_count) {
    std::lock_guard lock(mutex_);

    const auto new_type = LfgSelectionType(packed_dungeon_id);
    const bool can_preserve_existing = std::all_of(
        selected_dungeons_.begin(), selected_dungeons_.end(),
        [new_type, tracked_party_member_count](const std::uint32_t existing_packed_dungeon_id) {
            return CanPreserveSelectionType(new_type, tracked_party_member_count,
                                            existing_packed_dungeon_id);
        });

    if (!can_preserve_existing && !selected_dungeons_.empty()) {
        selected_dungeons_.clear();
        join_request_dirty_ = true;
    }

    if (std::find(selected_dungeons_.begin(), selected_dungeons_.end(), packed_dungeon_id) ==
        selected_dungeons_.end()) {
        selected_dungeons_.push_back(packed_dungeon_id);
        join_request_dirty_ = true;
    }
}

void LFGSystem::RemoveSelectedDungeon(const std::uint32_t packed_dungeon_id) {
    std::lock_guard lock(mutex_);

    const auto it =
        std::find(selected_dungeons_.begin(), selected_dungeons_.end(), packed_dungeon_id);
    if (it == selected_dungeons_.end()) {
        return;
    }

    selected_dungeons_.erase(it);
    join_request_dirty_ = true;
}

void LFGSystem::ClearSelectedDungeons() {
    std::lock_guard lock(mutex_);
    selected_dungeons_.clear();
    join_request_dirty_ = true;
}

bool LFGSystem::ContainsSelectedDungeon(const std::uint32_t packed_dungeon_id) const {
    std::lock_guard lock(mutex_);
    return std::find(selected_dungeons_.begin(), selected_dungeons_.end(), packed_dungeon_id) !=
           selected_dungeons_.end();
}

void LFGSystem::SetRewards(const std::vector<LFGReward>& rewards) {
    std::lock_guard lock(mutex_);
    rewards_ = rewards;
}

size_t LFGSystem::GetNumRewards() const {
    std::lock_guard lock(mutex_);
    return rewards_.size();
}

const LFGReward* LFGSystem::GetReward(size_t index) const {
    std::lock_guard lock(mutex_);
    if (index >= rewards_.size()) return nullptr;
    return &rewards_[index];
}

void LFGSystem::SetBootVote(const LFGBootVote& vote) {
    std::lock_guard lock(mutex_);
    boot_vote_ = vote;
}

const LFGBootVote* LFGSystem::GetBootVote() const {
    std::lock_guard lock(mutex_);
    if (boot_vote_.has_value()) return &boot_vote_.value();
    return nullptr;
}

std::optional<LFGBootVote> LFGSystem::GetBootVoteSnapshot() const {
    std::lock_guard lock(mutex_);
    return boot_vote_;
}

bool LFGSystem::HasBootVote() const {
    std::lock_guard lock(mutex_);
    return boot_vote_.has_value();
}

void LFGSystem::Reset() {
    std::lock_guard lock(mutex_);
    state_ = LFGState::None;
    avg_wait_ = 0;
    wait_time_ = 0;
    time_in_queue_ = 0;
    proposal_.reset();
    roles_ = 0;
    selected_dungeons_.clear();
    rewards_.clear();
    boot_vote_.reset();
    queue_info_ext_ = {};
    selected_roles_ = 0;
    comment_.clear();
    comment_send_window_anchor_ = CommentThrottleClock::time_point{};
    comment_send_attempts_ = 0;
    join_send_window_anchor_ = JoinThrottleClock::time_point{};
    join_send_attempts_ = 0;
    teleport_send_throttle_.Reset();
    join_request_active_ = false;
    join_request_dirty_ = false;
    boot_vote_send_window_anchor_ = CommentThrottleClock::time_point{};
    boot_vote_send_attempts_ = 0;
}

void LFGSystem::JoinQueue(const std::vector<uint32_t>& dungeonIds,
                          uint32_t roles) {
    std::lock_guard lock(mutex_);
    selected_dungeons_ = dungeonIds;
    selected_roles_ = roles;
    state_ = LFGState::Queued;
    join_request_active_ = true;
    join_request_dirty_ = false;
}

void LFGSystem::LeaveQueue() {
    std::lock_guard lock(mutex_);
    state_ = LFGState::None;
    join_request_active_ = false;
    join_request_dirty_ = false;
}

uint32_t LFGSystem::GetSelectedRoles() const {
    std::lock_guard lock(mutex_);
    return selected_roles_;
}

bool LFGSystem::HasRole(LFGRoleFlag flag) const {
    std::lock_guard lock(mutex_);
    return (selected_roles_ & static_cast<uint32_t>(flag)) != 0;
}

bool LFGSystem::HasActiveJoinRequest() const {
    std::lock_guard lock(mutex_);
    return join_request_active_;
}

bool LFGSystem::TryBeginJoinRequest() {
    return TryBeginJoinRequest(JoinThrottleClock::now());
}

bool LFGSystem::TryBeginJoinRequest(JoinThrottleClock::time_point now) {
    std::lock_guard lock(mutex_);
    if (join_request_active_ && !join_request_dirty_) {
        return false;
    }

    ++join_send_attempts_;
    if (join_send_attempts_ > 2) {
        if (join_send_window_anchor_ == JoinThrottleClock::time_point{} ||
            now - join_send_window_anchor_ >= std::chrono::seconds(10)) {
            join_send_window_anchor_ = now;
            join_send_attempts_ = 0;
        } else {
            return false;
        }
    }

    return true;
}

bool LFGSystem::TryPrepareTeleportSend() {
    return TryPrepareTeleportSend(core::GameClock::GetTickCountSeconds());
}

bool LFGSystem::TryPrepareTeleportSend(const double now_seconds) {
    std::lock_guard lock(mutex_);
    return teleport_send_throttle_.TryConsume(now_seconds, 2, 10.0);
}

void LFGSystem::SetQueueInfo(const LFGQueueInfo& info) {
    std::lock_guard lock(mutex_);
    queue_info_ext_ = info;
}

LFGQueueInfo LFGSystem::GetQueueInfo() const {
    std::lock_guard lock(mutex_);
    return queue_info_ext_;
}

uint32_t LFGSystem::GetWaitTime() const {
    std::lock_guard lock(mutex_);
    return queue_info_ext_.myWait;
}

void LFGSystem::SetComment(const std::string& comment) {
    std::lock_guard lock(mutex_);
    comment_ = comment;
}

std::string LFGSystem::GetComment() const {
    std::lock_guard lock(mutex_);
    return comment_;
}

bool LFGSystem::TryConsumeCommentSendThrottleToken() {
    return TryConsumeCommentSendThrottleToken(CommentThrottleClock::now());
}

bool LFGSystem::TryConsumeCommentSendThrottleToken(
    CommentThrottleClock::time_point now) {
    std::lock_guard lock(mutex_);
    ++comment_send_attempts_;
    if (comment_send_attempts_ > 2) {
        if (comment_send_window_anchor_ == CommentThrottleClock::time_point{} ||
            now - comment_send_window_anchor_ >= std::chrono::seconds(2)) {
            comment_send_window_anchor_ = now;
            comment_send_attempts_ = 0;
        } else if (comment_send_attempts_ > 2) {
            return false;
        }
    }
    return true;
}

bool LFGSystem::TryPrepareBootVoteSend(bool agree) {
    return TryPrepareBootVoteSend(agree, CommentThrottleClock::now());
}

bool LFGSystem::TryPrepareBootVoteSend(
    bool agree, CommentThrottleClock::time_point now) {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value() || !boot_vote_->in_progress) {
        return false;
    }

    ++boot_vote_send_attempts_;
    if (boot_vote_send_attempts_ > 2) {
        if (now - boot_vote_send_window_anchor_ < std::chrono::seconds(10)) {
            return false;
        }
        boot_vote_send_window_anchor_ = now;
        boot_vote_send_attempts_ = 0;
    }

    boot_vote_->my_vote = agree;
    return true;
}

void LFGSystem::SetBootVoteInProgress(bool inProgress) {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        boot_vote_.emplace();
    }
    boot_vote_->in_progress = inProgress;
}

bool LFGSystem::IsBootVoteInProgress() const {
    std::lock_guard lock(mutex_);
    return boot_vote_.has_value() && boot_vote_->in_progress;
}

void LFGSystem::VoteToBoot(bool agree) {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        boot_vote_.emplace();
    }
    boot_vote_->did_vote = true;
    boot_vote_->my_vote = agree;
    ++boot_vote_->votes_total;
    if (agree) {
        ++boot_vote_->agree_count;
    }
}

void LFGSystem::SetBootTarget(ObjectGuid target) {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        boot_vote_.emplace();
    }
    boot_vote_->target_guid = target.GetRawValue();
}

ObjectGuid LFGSystem::GetBootTarget() const {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        return ObjectGuid{};
    }
    return ObjectGuid(boot_vote_->target_guid);
}

uint32_t LFGSystem::GetBootVotesNeeded() const {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        return 0;
    }
    return boot_vote_->votes_needed;
}

void LFGSystem::SetBootVoteCount(uint32_t yes, uint32_t no) {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        boot_vote_.emplace();
    }
    boot_vote_->agree_count = yes;
    boot_vote_->votes_total = yes + no;
}

uint32_t LFGSystem::GetBootVoteYes() const {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value()) {
        return 0;
    }
    return boot_vote_->agree_count;
}

uint32_t LFGSystem::GetBootVoteNo() const {
    std::lock_guard lock(mutex_);
    if (!boot_vote_.has_value() || boot_vote_->votes_total < boot_vote_->agree_count) {
        return 0;
    }
    return boot_vote_->votes_total - boot_vote_->agree_count;
}

}

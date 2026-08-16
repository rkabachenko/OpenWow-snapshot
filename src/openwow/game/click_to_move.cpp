
#include "openwow/game/click_to_move.h"
#include <algorithm>
#include <cmath>

namespace openwow::game {

namespace {

ClickToMoveSystem::CompletedAction BuildCompletedAction(const CTMAction action,
                                                        const ObjectGuid target,
                                                        const float x,
                                                        const float y,
                                                        const float z) {
    return ClickToMoveSystem::CompletedAction{
        .action = action,
        .target = target,
        .destination = ClickToMoveSystem::Destination{x, y, z},
    };
}

}

void ClickToMoveSystem::MoveTo(float x, float y, float z) {
    StartAction(CTMAction::Move, x, y, z);
}

void ClickToMoveSystem::AttackUnit(ObjectGuid guid, float x, float y, float z) {
    StartAction(CTMAction::Attack, x, y, z, guid);
}

void ClickToMoveSystem::LootTarget(ObjectGuid guid, float x, float y, float z,
                                  const float arrival_threshold) {
    StartAction(CTMAction::Loot, x, y, z, guid);
    SetArrivalThreshold(arrival_threshold);
}

void ClickToMoveSystem::InteractWith(ObjectGuid guid, float x, float y, float z,
                                     float arrival_threshold) {
    StartAction(CTMAction::Interact, x, y, z, guid);
    SetArrivalThreshold(arrival_threshold);
}

void ClickToMoveSystem::FollowUnit(ObjectGuid guid) {
    StartAction(CTMAction::FollowUnit, 0.0f, 0.0f, 0.0f, guid);
}

void ClickToMoveSystem::FaceTo(float x, float y, float z) {
    StartAction(CTMAction::FaceTo, x, y, z);
}

void ClickToMoveSystem::Stop() {
    completed_action_.reset();
    ClearActiveState();
}

void ClickToMoveSystem::CancelInteraction(const ObjectGuid target) {
    const bool active_matches =
        action_ == CTMAction::Interact &&
        (target.IsEmpty() || target_ == target);
    const bool completed_matches =
        completed_action_.has_value() &&
        completed_action_->action == CTMAction::Interact &&
        (target.IsEmpty() || completed_action_->target == target);
    if (!active_matches && !completed_matches) {
        return;
    }

    completed_action_.reset();
    if (active_matches) {
        ClearActiveState();
    }
}

CTMAction ClickToMoveSystem::GetAction() const { return action_; }
bool ClickToMoveSystem::IsActive() const { return action_ != CTMAction::None; }

bool ClickToMoveSystem::CameraFollowsUnitDuringDrive() const {

    return action_ == CTMAction::FollowUnit;
}

std::uint32_t ClickToMoveSystem::GetActionGeneration() const {
    return action_generation_;
}

ClickToMoveSystem::Destination ClickToMoveSystem::GetDestination() const {
    return {dest_x_, dest_y_, dest_z_};
}

ObjectGuid ClickToMoveSystem::GetTarget() const { return target_; }

void ClickToMoveSystem::UpdateDestination(float x, float y, float z) {
    if (action_ == CTMAction::None) return;
    dest_x_ = x;
    dest_y_ = y;
    dest_z_ = z;
}

std::optional<ClickToMoveSystem::CompletedAction> ClickToMoveSystem::ConsumeCompletedAction() {
    auto completed = completed_action_;
    completed_action_.reset();
    return completed;
}

void ClickToMoveSystem::SetPath(std::vector<CTMWaypoint> waypoints) {
    completed_action_.reset();
    path_ = std::move(waypoints);
    current_waypoint_ = 0;
    has_previous_position_ = false;
    if (!path_.empty()) {
        dest_x_ = path_[0].x;
        dest_y_ = path_[0].y;
        dest_z_ = path_[0].z;
    }
}

const std::vector<CTMWaypoint>& ClickToMoveSystem::GetPath() const {
    return path_;
}

uint32_t ClickToMoveSystem::GetCurrentWaypoint() const {
    return current_waypoint_;
}

float ClickToMoveSystem::GetDistanceToDestination(float px, float py, float pz) const {
    const float dx = dest_x_ - px;
    const float dy = dest_y_ - py;
    const float dz = vertical_distance_included_ ? dest_z_ - pz : 0.0f;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool ClickToMoveSystem::IsAtDestination(float px, float py, float pz,
                                          float threshold) const {
    return GetDistanceToDestination(px, py, pz) <= threshold;
}

bool ClickToMoveSystem::ReachedDestination(const float player_x,
                                           const float player_y,
                                           const float player_z) const {
    if (IsAtDestination(player_x, player_y, player_z, arrival_threshold_)) {
        return true;
    }
    if (!has_previous_position_) {
        return false;
    }

    const float dx = player_x - previous_position_.x;
    const float dy = player_y - previous_position_.y;
    const float dz =
        vertical_distance_included_ ? player_z - previous_position_.z : 0.0f;
    const float length_squared = dx * dx + dy * dy + dz * dz;
    if (length_squared <= 0.0f) {
        return false;
    }

    const float to_destination_x = dest_x_ - previous_position_.x;
    const float to_destination_y = dest_y_ - previous_position_.y;
    const float to_destination_z =
        vertical_distance_included_ ? dest_z_ - previous_position_.z : 0.0f;
    const float t = std::clamp(
        (to_destination_x * dx + to_destination_y * dy +
         to_destination_z * dz) /
            length_squared,
        0.0f, 1.0f);
    const float closest_x = previous_position_.x + dx * t;
    const float closest_y = previous_position_.y + dy * t;
    const float closest_z = previous_position_.z + dz * t;
    const float miss_x = dest_x_ - closest_x;
    const float miss_y = dest_y_ - closest_y;
    const float miss_z = dest_z_ - closest_z;
    return miss_x * miss_x + miss_y * miss_y + miss_z * miss_z <=
           arrival_threshold_ * arrival_threshold_;
}

void ClickToMoveSystem::RememberPosition(const float player_x,
                                         const float player_y,
                                         const float player_z) {
    previous_position_ = {player_x, player_y, player_z};
    has_previous_position_ = true;
}

void  ClickToMoveSystem::SetArrivalThreshold(float threshold) {
    arrival_threshold_ = std::max(0.0f, threshold);
}

float ClickToMoveSystem::GetArrivalThreshold() const {
    return arrival_threshold_;
}

void ClickToMoveSystem::SetVerticalDistanceIncluded(const bool included) {
    vertical_distance_included_ = included;
}

bool ClickToMoveSystem::IsVerticalDistanceIncluded() const {
    return vertical_distance_included_;
}

void ClickToMoveSystem::SetEnabled(bool enabled) { enabled_ = enabled; }
bool ClickToMoveSystem::IsEnabled() const { return enabled_; }

void ClickToMoveSystem::Update(float , float playerX, float playerY, float playerZ) {
    if (!enabled_ || action_ == CTMAction::None) return;

    if (ReachedDestination(playerX, playerY, playerZ)) {
        if (!path_.empty() && current_waypoint_ + 1 < static_cast<uint32_t>(path_.size())) {
            ++current_waypoint_;
            dest_x_ = path_[current_waypoint_].x;
            dest_y_ = path_[current_waypoint_].y;
            dest_z_ = path_[current_waypoint_].z;
            has_previous_position_ = false;
        } else {
            completed_action_ =
                BuildCompletedAction(action_, target_, dest_x_, dest_y_, dest_z_);
            ClearActiveState();
            return;
        }
    }
    RememberPosition(playerX, playerY, playerZ);
}

void ClickToMoveSystem::Reset() {
    enabled_   = true;
    dest_x_    = 0.0f;
    dest_y_    = 0.0f;
    dest_z_    = 0.0f;
    arrival_threshold_ = 1.0f;
    completed_action_.reset();
    ClearActiveState();
}

void ClickToMoveSystem::ClearActiveState() {
    action_ = CTMAction::None;
    target_ = ObjectGuid{};
    path_.clear();
    current_waypoint_ = 0;
    arrival_threshold_ = 1.0f;
    has_previous_position_ = false;
}

void ClickToMoveSystem::StartAction(CTMAction action, float x, float y, float z,
                                     ObjectGuid guid) {
    if (!enabled_) return;
    completed_action_.reset();
    ++action_generation_;
    arrival_threshold_ = 1.0f;
    action_ = action;
    dest_x_ = x;
    dest_y_ = y;
    dest_z_ = z;
    target_ = guid;
    path_.clear();
    current_waypoint_ = 0;
    has_previous_position_ = false;
}

}

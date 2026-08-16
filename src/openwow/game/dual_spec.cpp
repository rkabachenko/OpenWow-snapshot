
#include "openwow/game/dual_spec.h"

#include <algorithm>

namespace openwow::game {

void DualSpecManager::SetActiveSpec(uint32_t index) {
    if (index < 2) active_spec_ = index;
}

uint32_t DualSpecManager::GetActiveSpec() const {
    return active_spec_;
}

uint32_t DualSpecManager::GetSpecCount() const {
    return dual_spec_unlocked_ ? 2 : 1;
}

bool DualSpecManager::HasDualSpec() const {
    return dual_spec_unlocked_;
}

void DualSpecManager::SetDualSpecUnlocked(bool unlocked) {
    dual_spec_unlocked_ = unlocked;
}

bool DualSpecManager::IsDualSpecUnlocked() const {
    return dual_spec_unlocked_;
}

uint32_t DualSpecManager::GetDualSpecCost() const {
    return kDualSpecCost;
}

void DualSpecManager::SetSpecName(uint32_t specIndex, std::string name) {
    if (specIndex < 2) spec_names_[specIndex] = std::move(name);
}

std::string DualSpecManager::GetSpecName(uint32_t specIndex) const {
    if (specIndex >= 2) return {};
    return spec_names_[specIndex];
}

void DualSpecManager::SetPointsSpent(uint32_t specIndex, uint32_t tab0,
                                      uint32_t tab1, uint32_t tab2) {
    if (specIndex >= 2) return;
    points_spent_[specIndex] = {tab0, tab1, tab2};
}

PointsSpent DualSpecManager::GetPointsSpent(uint32_t specIndex) const {
    if (specIndex >= 2) return {};
    return points_spent_[specIndex];
}

uint32_t DualSpecManager::GetPrimaryTree(uint32_t specIndex) const {
    if (specIndex >= 2) return 0;
    const auto& ps = points_spent_[specIndex];

    if (ps.tab0 >= ps.tab1 && ps.tab0 >= ps.tab2) return 0;
    if (ps.tab1 >= ps.tab0 && ps.tab1 >= ps.tab2) return 1;
    return 2;
}

uint32_t DualSpecManager::GetTotalPoints(uint32_t specIndex) const {
    if (specIndex >= 2) return 0;
    const auto& ps = points_spent_[specIndex];
    return ps.tab0 + ps.tab1 + ps.tab2;
}

bool DualSpecManager::CanSwapSpec() const {
    return dual_spec_unlocked_ && can_swap_ && swap_cooldown_ <= 0.0f;
}

void DualSpecManager::SetCanSwap(bool can_swap) {
    can_swap_ = can_swap;
}

float DualSpecManager::GetSwapCooldown() const {
    return swap_cooldown_;
}

void DualSpecManager::SetSwapCooldown(float seconds) {
    swap_cooldown_ = std::max(0.0f, seconds);
}

void DualSpecManager::Update(float dt) {
    if (swap_cooldown_ > 0.0f) {
        swap_cooldown_ = std::max(0.0f, swap_cooldown_ - dt);
    }
}

void DualSpecManager::Reset() {
    active_spec_ = 0;
    dual_spec_unlocked_ = false;
    spec_names_[0] = "Primary";
    spec_names_[1] = "Secondary";
    points_spent_[0] = {};
    points_spent_[1] = {};
    can_swap_ = true;
    swap_cooldown_ = 0.0f;
}

}

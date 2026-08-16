
#include "openwow/game/buff_frame_display.h"

#include <algorithm>

namespace openwow::game {

void BuffFrameDisplay::SetBuffs(std::vector<BuffFrameIcon> buffs) {
    buffs_ = std::move(buffs);
}

void BuffFrameDisplay::SetDebuffs(std::vector<BuffFrameIcon> debuffs) {
    debuffs_ = std::move(debuffs);
}

std::vector<BuffFrameIcon> BuffFrameDisplay::GetBuffs() const { return buffs_; }
std::vector<BuffFrameIcon> BuffFrameDisplay::GetDebuffs() const { return debuffs_; }
uint32_t BuffFrameDisplay::GetBuffCount() const { return static_cast<uint32_t>(buffs_.size()); }
uint32_t BuffFrameDisplay::GetDebuffCount() const { return static_cast<uint32_t>(debuffs_.size()); }

std::optional<BuffFrameIcon> BuffFrameDisplay::GetBuff(uint32_t auraIndex) const {
    for (auto const& b : buffs_) {
        if (b.auraIndex == auraIndex) return b;
    }
    return std::nullopt;
}

std::optional<BuffFrameIcon> BuffFrameDisplay::GetDebuff(uint32_t auraIndex) const {
    for (auto const& d : debuffs_) {
        if (d.auraIndex == auraIndex) return d;
    }
    return std::nullopt;
}

bool BuffFrameDisplay::HasBuff(uint32_t spellId) const {
    return std::any_of(buffs_.begin(), buffs_.end(),
        [spellId](auto const& b) { return b.spellId == spellId; });
}

bool BuffFrameDisplay::HasDebuff(uint32_t spellId) const {
    return std::any_of(debuffs_.begin(), debuffs_.end(),
        [spellId](auto const& d) { return d.spellId == spellId; });
}

void BuffFrameDisplay::UpdateTimers(float deltaTime) {
    auto tick = [&](std::vector<BuffFrameIcon>& list) {
        for (auto& icon : list) {
            if (icon.timeRemaining < 0.0f) continue;
            icon.timeRemaining -= deltaTime;
            if (icon.timeRemaining < 0.0f) icon.timeRemaining = 0.0f;
        }
    };
    tick(buffs_);
    tick(debuffs_);
}

std::vector<BuffFrameIcon> BuffFrameDisplay::GetExpiredBuffs() const {
    std::vector<BuffFrameIcon> expired;
    auto collect = [&](std::vector<BuffFrameIcon> const& list) {
        for (auto const& icon : list) {
            if (icon.timeRemaining == 0.0f) expired.push_back(icon);
        }
    };
    collect(buffs_);
    collect(debuffs_);
    return expired;
}

void BuffFrameDisplay::RemoveBuff(uint32_t auraIndex) {
    buffs_.erase(
        std::remove_if(buffs_.begin(), buffs_.end(),
            [auraIndex](auto const& b) { return b.auraIndex == auraIndex; }),
        buffs_.end());
}

void BuffFrameDisplay::RemoveDebuff(uint32_t auraIndex) {
    debuffs_.erase(
        std::remove_if(debuffs_.begin(), debuffs_.end(),
            [auraIndex](auto const& d) { return d.auraIndex == auraIndex; }),
        debuffs_.end());
}

void BuffFrameDisplay::ClearAll() {
    buffs_.clear();
    debuffs_.clear();
}

void BuffFrameDisplay::SetMaxVisible(uint32_t maxBuffs, uint32_t maxDebuffs) {
    maxVisibleBuffs_   = maxBuffs;
    maxVisibleDebuffs_ = maxDebuffs;
}

std::vector<BuffFrameIcon> BuffFrameDisplay::GetVisibleBuffs() const {
    if (buffs_.size() <= maxVisibleBuffs_) return buffs_;
    return {buffs_.begin(), buffs_.begin() + maxVisibleBuffs_};
}

std::vector<BuffFrameIcon> BuffFrameDisplay::GetVisibleDebuffs() const {
    if (debuffs_.size() <= maxVisibleDebuffs_) return debuffs_;
    return {debuffs_.begin(), debuffs_.begin() + maxVisibleDebuffs_};
}

void BuffFrameDisplay::SortByTimeRemaining() {

    auto cmp = [](BuffFrameIcon const& a, BuffFrameIcon const& b) {
        bool aPerm = a.timeRemaining < 0.0f;
        bool bPerm = b.timeRemaining < 0.0f;
        if (aPerm != bPerm) return !aPerm;
        return a.timeRemaining < b.timeRemaining;
    };
    std::sort(buffs_.begin(), buffs_.end(), cmp);
    std::sort(debuffs_.begin(), debuffs_.end(), cmp);
}

}

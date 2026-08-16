
#include "openwow/game/buff_frame.h"

#include <algorithm>
#include <numeric>

namespace openwow::game {

void BuffFrameData::SetBuffs(std::vector<BuffDisplayEntry> buffs) {
    buffs_ = std::move(buffs);
}

void BuffFrameData::AddBuff(const BuffDisplayEntry& buff) {

    for (auto& b : buffs_) {
        if (b.auraIndex == buff.auraIndex) {
            b = buff;
            return;
        }
    }
    buffs_.push_back(buff);
}

bool BuffFrameData::RemoveBuff(uint32_t auraIndex) {
    auto it = std::find_if(buffs_.begin(), buffs_.end(),
                           [auraIndex](const BuffDisplayEntry& b) {
                               return b.auraIndex == auraIndex;
                           });
    if (it == buffs_.end()) return false;
    buffs_.erase(it);
    return true;
}

bool BuffFrameData::RemoveBuffBySpellId(uint32_t spellId) {
    auto it = std::find_if(buffs_.begin(), buffs_.end(),
                           [spellId](const BuffDisplayEntry& b) {
                               return b.spellId == spellId;
                           });
    if (it == buffs_.end()) return false;
    buffs_.erase(it);
    return true;
}

uint32_t BuffFrameData::RemoveExpiredBuffs() {
    uint32_t removed = 0;
    auto it = buffs_.begin();
    while (it != buffs_.end()) {

        if (it->duration > 0.0f && it->remaining <= 0.0f) {
            it = buffs_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<BuffDisplayEntry> BuffFrameData::GetBuffs() const {
    return buffs_;
}

std::optional<BuffDisplayEntry> BuffFrameData::GetBuff(
    uint32_t auraIndex) const {
    for (const auto& b : buffs_) {
        if (b.auraIndex == auraIndex) return b;
    }
    return std::nullopt;
}

uint32_t BuffFrameData::GetBuffCount() const {
    return static_cast<uint32_t>(buffs_.size());
}

bool BuffFrameData::HasBuff(uint32_t spellId) const {
    return std::any_of(buffs_.begin(), buffs_.end(),
                       [spellId](const BuffDisplayEntry& b) {
                           return b.spellId == spellId;
                       });
}

std::optional<BuffDisplayEntry> BuffFrameData::GetBuffBySpellId(
    uint32_t spellId) const {
    for (const auto& b : buffs_) {
        if (b.spellId == spellId) return b;
    }
    return std::nullopt;
}

std::vector<BuffDisplayEntry> BuffFrameData::GetMyBuffs() const {
    std::vector<BuffDisplayEntry> result;
    result.reserve(buffs_.size());
    for (const auto& b : buffs_) {
        if (b.isMine) result.push_back(b);
    }
    return result;
}

std::vector<BuffDisplayEntry> BuffFrameData::GetStealableBuffs() const {
    std::vector<BuffDisplayEntry> result;
    for (const auto& b : buffs_) {
        if (b.isStealable) result.push_back(b);
    }
    return result;
}

std::vector<BuffDisplayEntry> BuffFrameData::GetExpiringBuffs(
    float threshold) const {
    if (threshold <= 0.0f) return {};

    std::vector<BuffDisplayEntry> result;
    for (const auto& b : buffs_) {
        if (b.remaining > 0.0f && b.remaining < threshold) {
            result.push_back(b);
        }
    }
    return result;
}

std::vector<BuffDisplayEntry> BuffFrameData::GetConsolidatedBuffs(
    float minDuration) const {
    std::vector<BuffDisplayEntry> result;
    for (const auto& b : buffs_) {

        bool isPermanent = (b.duration == 0.0f && b.remaining == 0.0f);
        bool isLong = (b.duration >= minDuration);
        if (!b.isMine && (isPermanent || isLong)) {
            result.push_back(b);
        }
    }
    return result;
}

void BuffFrameData::Update(float dt) {
    if (dt <= 0.0f) return;
    for (auto& b : buffs_) {
        if (b.remaining > 0.0f) {
            b.remaining -= dt;
            if (b.remaining < 0.0f) b.remaining = 0.0f;
        }
    }
}

void BuffFrameData::SortByTimeRemaining() {
    std::sort(buffs_.begin(), buffs_.end(),
              [](const BuffDisplayEntry& a, const BuffDisplayEntry& b) {
                  return a.remaining < b.remaining;
              });
}

void BuffFrameData::SortByName() {
    std::sort(buffs_.begin(), buffs_.end(),
              [](const BuffDisplayEntry& a, const BuffDisplayEntry& b) {
                  return a.name < b.name;
              });
}

float BuffFrameData::GetTotalDuration() const {
    float total = 0.0f;
    for (const auto& b : buffs_) {
        total += b.remaining;
    }
    return total;
}

uint32_t BuffFrameData::GetMaxAuraIndex() const {
    uint32_t maxIdx = 0;
    for (const auto& b : buffs_) {
        if (b.auraIndex > maxIdx) maxIdx = b.auraIndex;
    }
    return maxIdx;
}

void BuffFrameData::Clear() {
    buffs_.clear();
}

}

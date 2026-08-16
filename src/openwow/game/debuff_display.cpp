
#include "openwow/game/debuff_display.h"

#include <algorithm>
#include <cstdio>
#include <numeric>

namespace openwow::game {

void DebuffDisplayData::SetDebuffs(std::vector<DebuffDisplayEntry> debuffs) {
    debuffs_ = std::move(debuffs);
}

std::vector<DebuffDisplayEntry> DebuffDisplayData::GetDebuffs() const {
    return debuffs_;
}

std::optional<DebuffDisplayEntry> DebuffDisplayData::GetDebuff(uint32_t auraIndex) const {
    for (const auto& d : debuffs_) {
        if (d.auraIndex == auraIndex) return d;
    }
    return std::nullopt;
}

uint32_t DebuffDisplayData::GetDebuffCount() const {
    return static_cast<uint32_t>(debuffs_.size());
}

bool DebuffDisplayData::HasDebuff(uint32_t spellId) const {
    for (const auto& d : debuffs_) {
        if (d.spellId == spellId) return true;
    }
    return false;
}

std::vector<DebuffDisplayEntry> DebuffDisplayData::GetDebuffsByType(DebuffDisplayType type) const {
    std::vector<DebuffDisplayEntry> result;
    for (const auto& d : debuffs_) {
        if (d.debuffType == type) result.push_back(d);
    }
    return result;
}

std::vector<DebuffDisplayEntry> DebuffDisplayData::GetDispellableDebuffs() const {
    std::vector<DebuffDisplayEntry> result;
    for (const auto& d : debuffs_) {
        if (d.debuffType != DebuffDisplayType::Physical &&
            d.debuffType != DebuffDisplayType::None) {
            result.push_back(d);
        }
    }
    return result;
}

std::vector<DebuffDisplayEntry> DebuffDisplayData::GetMyDebuffs() const {
    std::vector<DebuffDisplayEntry> result;
    for (const auto& d : debuffs_) {
        if (d.isMine) result.push_back(d);
    }
    return result;
}

uint32_t DebuffDisplayData::GetBorderColor(DebuffDisplayType type) {

    switch (type) {
        case DebuffDisplayType::Magic:   return 0xFF3399FF;
        case DebuffDisplayType::Curse:   return 0xFF9900CC;
        case DebuffDisplayType::Disease: return 0xFF996600;
        case DebuffDisplayType::Poison:  return 0xFF009900;
        case DebuffDisplayType::Physical:
        case DebuffDisplayType::None:
        default:                         return 0xFFCC0000;
    }
}

void DebuffDisplayData::Update(float dt) {
    for (auto& d : debuffs_) {
        if (d.remaining > 0.0f) {
            d.remaining -= dt;
            if (d.remaining < 0.0f) d.remaining = 0.0f;
        }
    }
}

void DebuffDisplayData::Clear() {
    debuffs_.clear();
}

void DebuffDisplayData::SortByDuration() {
    std::sort(debuffs_.begin(), debuffs_.end(),
              [](const DebuffDisplayEntry& a, const DebuffDisplayEntry& b) {
                  return a.duration > b.duration;
              });
}

void DebuffDisplayData::SortByTimeRemaining() {
    std::sort(debuffs_.begin(), debuffs_.end(),
              [](const DebuffDisplayEntry& a, const DebuffDisplayEntry& b) {
                  return a.remaining < b.remaining;
              });
}

std::vector<DebuffDisplayEntry> DebuffDisplayData::GetExpiringSoon(
    float thresholdSeconds) const {
    std::vector<DebuffDisplayEntry> result;
    for (const auto& d : debuffs_) {
        if (d.remaining > 0.0f && d.remaining <= thresholdSeconds) {
            result.push_back(d);
        }
    }
    return result;
}

void DebuffDisplayData::RemoveExpired() {
    std::erase_if(debuffs_, [](const DebuffDisplayEntry& d) {
        return d.duration > 0.0f && d.remaining <= 0.0f;
    });
}

std::optional<DebuffDisplayEntry> DebuffDisplayData::GetLongestDebuff() const {
    if (debuffs_.empty()) return std::nullopt;
    auto it = std::max_element(
        debuffs_.begin(), debuffs_.end(),
        [](const DebuffDisplayEntry& a, const DebuffDisplayEntry& b) {
            return a.remaining < b.remaining;
        });
    return *it;
}

uint32_t DebuffDisplayData::GetHighestStackCount() const {
    uint32_t maxStack = 0;
    for (const auto& d : debuffs_) {
        if (d.count > maxStack) maxStack = d.count;
    }
    return maxStack;
}

uint32_t DebuffDisplayData::GetDebuffCountByType(DebuffDisplayType type) const {
    uint32_t count = 0;
    for (const auto& d : debuffs_) {
        if (d.debuffType == type) ++count;
    }
    return count;
}

float DebuffDisplayData::GetAverageRemainingTime() const {
    if (debuffs_.empty()) return 0.0f;
    float total = 0.0f;
    for (const auto& d : debuffs_) {
        total += d.remaining;
    }
    return total / static_cast<float>(debuffs_.size());
}

std::string DebuffDisplayData::FormatRemainingTime(float seconds) {
    if (seconds <= 0.0f) return "0s";

    char buf[64];
    if (seconds >= 3600.0f) {
        int h = static_cast<int>(seconds) / 3600;
        int m = (static_cast<int>(seconds) % 3600) / 60;
        std::snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    } else if (seconds >= 60.0f) {
        int m = static_cast<int>(seconds) / 60;
        int s = static_cast<int>(seconds) % 60;
        std::snprintf(buf, sizeof(buf), "%dm %ds", m, s);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0fs", seconds);
    }
    return std::string(buf);
}

void DebuffDisplayData::RemoveDebuff(uint32_t auraIndex) {
    std::erase_if(debuffs_, [auraIndex](const DebuffDisplayEntry& d) {
        return d.auraIndex == auraIndex;
    });
}

void DebuffDisplayData::AddDebuff(const DebuffDisplayEntry& entry) {

    for (auto& d : debuffs_) {
        if (d.auraIndex == entry.auraIndex) {
            d = entry;
            return;
        }
    }
    debuffs_.push_back(entry);
}

bool DebuffDisplayData::HasDispellableDebuffs() const {
    for (const auto& d : debuffs_) {
        if (d.debuffType != DebuffDisplayType::Physical &&
            d.debuffType != DebuffDisplayType::None) {
            return true;
        }
    }
    return false;
}

}

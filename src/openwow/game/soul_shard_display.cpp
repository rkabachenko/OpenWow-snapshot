
#include "openwow/game/soul_shard_display.h"

#include <algorithm>

namespace openwow::game {

void SoulShardDisplay::SetShardCount(std::uint32_t count) {
    shard_count_ = std::min(count, kMaxShards);
}

std::uint32_t SoulShardDisplay::GetShardCount() const {
    return shard_count_;
}

std::uint32_t SoulShardDisplay::GetMaxShards() const {
    return kMaxShards;
}

bool SoulShardDisplay::HasShards() const {
    return shard_count_ > 0;
}

bool SoulShardDisplay::IsLow() const {
    return shard_count_ < kWarningThreshold;
}

void SoulShardDisplay::SetBagSlots(std::uint32_t soulBagSlots) {
    bag_slots_ = soulBagSlots;
}

std::uint32_t SoulShardDisplay::GetBagSlots() const {
    return bag_slots_;
}

float SoulShardDisplay::GetBagOccupancy() const {
    if (bag_slots_ == 0) return 0.0f;
    return static_cast<float>(shard_count_) / static_cast<float>(bag_slots_);
}

void SoulShardDisplay::SetEnabled(bool enabled) {
    enabled_ = enabled;
}

bool SoulShardDisplay::IsEnabled() const {
    return enabled_;
}

SoulShardColor SoulShardDisplay::GetDisplayColor() const {
    return {0.58f, 0.0f, 0.82f};
}

std::uint32_t SoulShardDisplay::GetWarningThreshold() const {
    return kWarningThreshold;
}

bool SoulShardDisplay::ConsumeShards(std::uint32_t count) {
    if (count > shard_count_) return false;
    shard_count_ -= count;
    total_consumed_ += count;
    return true;
}

void SoulShardDisplay::Reset() {
    shard_count_ = 0;
    bag_slots_ = 0;
    enabled_ = false;
    total_consumed_ = 0;
    spell_costs_.clear();
}

void SoulShardDisplay::SetSpellCost(std::uint32_t spellId,
                                    std::uint32_t shardCost) {

    for (auto& [id, cost] : spell_costs_) {
        if (id == spellId) {
            cost = shardCost;
            return;
        }
    }
    spell_costs_.emplace_back(spellId, shardCost);
}

std::uint32_t SoulShardDisplay::GetSpellCost(std::uint32_t spellId) const {
    for (const auto& [id, cost] : spell_costs_) {
        if (id == spellId) return cost;
    }
    return 0;
}

bool SoulShardDisplay::CanAfford(std::uint32_t spellId) const {
    std::uint32_t cost = GetSpellCost(spellId);
    if (cost == 0) return true;
    return shard_count_ >= cost;
}

std::uint32_t SoulShardDisplay::GetTotalConsumed() const {
    return total_consumed_;
}

float SoulShardDisplay::GetFillPercent() const {
    if (bag_slots_ == 0) return 0.0f;
    return static_cast<float>(shard_count_) /
           static_cast<float>(bag_slots_);
}

std::uint32_t SoulShardDisplay::GetShardDeficit() const {
    if (bag_slots_ == 0) return 0;
    if (shard_count_ >= bag_slots_) return 0;
    return bag_slots_ - shard_count_;
}

bool SoulShardDisplay::IsFull() const {
    return bag_slots_ > 0 && shard_count_ >= bag_slots_;
}

}

#pragma once

#include <cstdint>
#include <vector>

namespace openwow::game {

struct SoulShardColor {
    float r = 0.58f;
    float g = 0.0f;
    float b = 0.82f;
};

class SoulShardDisplay {
 public:
    static constexpr std::uint32_t kMaxShards        = 32;
    static constexpr std::uint32_t kWarningThreshold = 3;

    void SetShardCount(std::uint32_t count);
    [[nodiscard]] std::uint32_t GetShardCount() const;
    [[nodiscard]] std::uint32_t GetMaxShards() const;
    [[nodiscard]] bool HasShards() const;
    [[nodiscard]] bool IsLow() const;

    void SetBagSlots(std::uint32_t soulBagSlots);
    [[nodiscard]] std::uint32_t GetBagSlots() const;
    [[nodiscard]] float GetBagOccupancy() const;

    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsEnabled() const;

    [[nodiscard]] SoulShardColor GetDisplayColor() const;
    [[nodiscard]] std::uint32_t GetWarningThreshold() const;

    bool ConsumeShards(std::uint32_t count);

    void SetSpellCost(std::uint32_t spellId, std::uint32_t shardCost);

    [[nodiscard]] std::uint32_t GetSpellCost(std::uint32_t spellId) const;

    [[nodiscard]] bool CanAfford(std::uint32_t spellId) const;

    [[nodiscard]] std::uint32_t GetTotalConsumed() const;

    [[nodiscard]] float GetFillPercent() const;

    [[nodiscard]] std::uint32_t GetShardDeficit() const;

    [[nodiscard]] bool IsFull() const;

    void Reset();

 private:
    std::uint32_t shard_count_ = 0;
    std::uint32_t bag_slots_   = 0;
    bool enabled_              = false;
    std::uint32_t total_consumed_ = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spell_costs_;
};

}

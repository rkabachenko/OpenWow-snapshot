
#pragma once

#include "openwow/game/faction_system.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::game {

struct StandingThreshold {
    FactionStanding standing;
    int32_t min_rep;
    int32_t tier_size;
};

inline constexpr std::array<StandingThreshold, 8> kStandingThresholds = {{
    {FactionStanding::Hated,      -42000, 36000},
    {FactionStanding::Hostile,     -6000,  3000},
    {FactionStanding::Unfriendly,  -3000,  3000},
    {FactionStanding::Neutral,         0,  3000},
    {FactionStanding::Friendly,     3000,  6000},
    {FactionStanding::Honored,      9000, 12000},
    {FactionStanding::Revered,     21000, 21000},
    {FactionStanding::Exalted,     42000,  1000},
}};

struct BarPosition {
    FactionStanding standing = FactionStanding::Neutral;
    int32_t current  = 0;
    int32_t maximum  = 0;
};

enum class ReputationExpansion : uint8_t {
    Classic = 0,
    TBC     = 1,
    WotLK   = 2,
    Other   = 3,
};

struct StandingColor {
    uint8_t r, g, b, a;
};

class ReputationManager {
 public:
    static ReputationManager& Get();

    [[nodiscard]] static BarPosition ComputeBarPosition(int32_t raw_rep);

    [[nodiscard]] static std::string GetStandingName(FactionStanding s);

    [[nodiscard]] static StandingColor GetStandingColor(FactionStanding s);

    using Visitor = std::function<void(const FactionInfo&)>;

    void ForEach(const Visitor& fn) const;
    void ForEachAtWar(const Visitor& fn) const;
    void ForEachByExpansion(ReputationExpansion exp, const Visitor& fn) const;
    void ForEachNonHeader(const Visitor& fn) const;

    [[nodiscard]] uint32_t GetWatchedFactionId() const;
    [[nodiscard]] const FactionInfo* GetWatchedFaction() const;

    [[nodiscard]] bool CanToggleAtWar(uint32_t faction_id) const;

    [[nodiscard]] size_t GetExaltedCount() const;
    [[nodiscard]] size_t GetTotalFactionCount() const;
    [[nodiscard]] size_t GetVisibleFactionCount() const;

    [[nodiscard]] std::vector<const FactionInfo*> GetSortedFactions() const;

    [[nodiscard]] static ReputationExpansion ClassifyExpansion(uint32_t faction_id);

 private:
    ReputationManager() = default;
};

}

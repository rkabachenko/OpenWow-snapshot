
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class RepStandingLevel : uint8_t {
    Hated      = 0,
    Hostile    = 1,
    Unfriendly = 2,
    Neutral    = 3,
    Friendly   = 4,
    Honored    = 5,
    Revered    = 6,
    Exalted    = 7,
};

struct RepDetailEntry {
    uint32_t    factionId    = 0;
    std::string factionName;
    std::string description;
    RepStandingLevel level   = RepStandingLevel::Neutral;
    int32_t     current      = 0;
    int32_t     max          = 1;
    bool        atWar        = false;
    bool        inactive     = false;
    bool        watched      = false;
    uint32_t    headerIndex  = 0;
};

class RepDetailSystem {
public:
    void SetFaction(const RepDetailEntry& entry);
    [[nodiscard]] std::optional<RepDetailEntry> GetFaction(uint32_t factionId) const;
    [[nodiscard]] std::vector<RepDetailEntry> GetAllFactions() const;
    [[nodiscard]] std::vector<RepDetailEntry> GetFactionsByHeader(uint32_t headerIndex) const;

    void SetWatched(uint32_t factionId);
    [[nodiscard]] std::optional<RepDetailEntry> GetWatched() const;

    void ToggleAtWar(uint32_t factionId);
    void ToggleInactive(uint32_t factionId);

    [[nodiscard]] static std::string GetStandingLabel(RepStandingLevel level);

    [[nodiscard]] float GetProgressPercent(uint32_t factionId) const;

    [[nodiscard]] size_t GetFactionCount() const;
    [[nodiscard]] size_t GetExaltedCount() const;

    void Reset();

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, RepDetailEntry> factions_;
    std::optional<uint32_t> watched_faction_;
};

}

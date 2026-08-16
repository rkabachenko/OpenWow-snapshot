
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace openwow::game {

enum class FactionStanding : uint8_t {
    Hated      = 0,
    Hostile    = 1,
    Unfriendly = 2,
    Neutral    = 3,
    Friendly   = 4,
    Honored    = 5,
    Revered    = 6,
    Exalted    = 7,
};

struct FactionInfo {
    uint32_t faction_id       = 0;
    std::string name;
    std::string description;
    FactionStanding standing  = FactionStanding::Neutral;
    int32_t reputation        = 0;
    int32_t bar_min           = 0;
    int32_t bar_max           = 0;
    uint8_t flags             = 0;
    bool is_header            = false;
    bool is_collapsed         = false;
    bool at_war               = false;
    bool can_toggle_at_war    = false;
    bool is_inactive          = false;
    bool is_watched           = false;
    bool is_child             = false;
    uint8_t header_index      = 0;
};

struct RepChange {
    uint32_t faction_id = 0;
    int32_t  amount     = 0;
};

class FactionSystem {
 public:
    static FactionSystem& Get();

    void SetFactions(const std::vector<FactionInfo>& factions);
    [[nodiscard]] size_t GetNumFactions() const;
    [[nodiscard]] const FactionInfo* GetFaction(size_t index) const;
    [[nodiscard]] const FactionInfo* GetFactionById(uint32_t factionId) const;

    [[nodiscard]] static std::string StandingText(FactionStanding standing);
    [[nodiscard]] static FactionStanding StandingFromRep(int32_t rep);

    void SetWatchedFaction(uint32_t factionId);
    [[nodiscard]] uint32_t GetWatchedFaction() const;

    void SetSelectedFactionByIndex(size_t index);
    void ClearSelectedFaction();
    [[nodiscard]] int GetSelectedFactionIndex() const;

    void SetCollapsed(size_t index, bool collapsed);

    void SetAtWar(size_t index, bool atWar);
    void SetInactive(size_t index, bool inactive);

    void PushRepChange(const RepChange& change);
    bool PopRepChange(RepChange& out);

    void Reset();

 private:
    FactionSystem() = default;

    std::vector<FactionInfo> factions_;
    uint32_t watched_faction_ = 0;
    std::optional<uint32_t> selected_faction_id_;
    std::queue<RepChange> rep_changes_;
    mutable std::mutex mutex_;
};

}

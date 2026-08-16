#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

struct ThreatTableEntry {
    ObjectGuid unitGuid;
    std::string name;
    float threatAmount = 0.0f;
    float threatPercent = 0.0f;
    bool isTanking = false;
    std::uint32_t rank = 0;
    float rawTPS = 0.0f;
};

class ThreatTableDisplay {
 public:
    void SetTarget(ObjectGuid target);
    [[nodiscard]] ObjectGuid GetTarget() const;

    void SetEntries(std::vector<ThreatTableEntry> entries);
    [[nodiscard]] const std::vector<ThreatTableEntry>& GetEntries() const;

    [[nodiscard]] std::optional<ThreatTableEntry> GetTopThreat() const;
    [[nodiscard]] std::optional<ThreatTableEntry> GetPlayerThreat(
        ObjectGuid playerGuid) const;

    [[nodiscard]] std::size_t GetEntryCount() const;
    [[nodiscard]] std::uint32_t GetPlayerRank(ObjectGuid playerGuid) const;
    [[nodiscard]] float GetPlayerThreatPercent(ObjectGuid playerGuid) const;
    [[nodiscard]] bool IsPlayerTanking(ObjectGuid playerGuid) const;

    void SetUpdateRate(float hz);
    [[nodiscard]] float GetUpdateRate() const;

    bool ShouldUpdate(float dt);

    void Sort();

    void Clear();

 private:
    ObjectGuid target_;
    std::vector<ThreatTableEntry> entries_;
    float updateRateHz_ = 4.0f;
    float updateAccumulator_ = 0.0f;
};

}

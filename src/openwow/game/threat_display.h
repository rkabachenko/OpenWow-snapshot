#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class ThreatDisplayStatus : std::uint8_t {
    ThreatSafe         = 0,
    ThreatWarning      = 1,
    ThreatHigh         = 2,
    ThreatPullingAggro = 3,
    ThreatHasAggro     = 4,
};

struct ThreatDisplayEntry {
    ObjectGuid  unitGuid;
    std::string unitName;
    std::uint32_t threatValue = 0;
    float       threatPercent = 0.0f;
    bool        isTanking     = false;
    std::uint8_t classId      = 0;
};

class ThreatDisplay {
 public:
    static ThreatDisplay& Get();

    void SetMyThreat(const ObjectGuid& target, std::uint32_t threat,
                     float percent);
    [[nodiscard]] float GetMyThreatPercent() const;
    [[nodiscard]] ThreatDisplayStatus GetMyThreatStatus() const;

    void SetThreatList(const ObjectGuid& target,
                       std::vector<ThreatDisplayEntry> list);
    [[nodiscard]] const std::vector<ThreatDisplayEntry>& GetThreatList() const;
    [[nodiscard]] ObjectGuid GetThreatTarget() const;

    [[nodiscard]] bool IsTanking() const;
    [[nodiscard]] float GetAggroThreshold() const;
    void SetIsMelee(bool melee);
    [[nodiscard]] bool IsMelee() const;
    [[nodiscard]] float GetThreatPct() const;

    [[nodiscard]] bool IsOnThreatList() const;
    [[nodiscard]] std::size_t GetListSize() const;

    [[nodiscard]] std::optional<ThreatDisplayEntry> GetTopThreatEntry() const;

    void SortByThreat();

    [[nodiscard]] std::size_t GetMyRank() const;

    [[nodiscard]] static std::uint32_t GetColorForStatus(ThreatDisplayStatus status);

    [[nodiscard]] static std::string GetStatusLabel(ThreatDisplayStatus status);

    [[nodiscard]] static std::string FormatThreatValue(std::uint32_t threat);
    void Reset();

 private:
    ThreatDisplayStatus ClassifyThreat(float pct) const;

    ObjectGuid target_;
    std::uint32_t my_threat_value_ = 0;
    float my_threat_percent_ = 0.0f;
    std::vector<ThreatDisplayEntry> threat_list_;
    bool is_melee_ = true;
};

}

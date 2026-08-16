#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace openwow::game {

enum class DiscoveryType : std::uint8_t {
    Zone    = 0,
    SubZone = 1,
    Area    = 2,
    Poi     = 3,
};

struct DiscoveryEvent {
    std::uint32_t  areaId    = 0;
    std::string    name;
    std::uint32_t  xpReward  = 0;
    DiscoveryType  type      = DiscoveryType::Area;
    float          timestamp = 0.0f;
};

class DiscoverySystem {
 public:
    DiscoverySystem() = default;

    static constexpr float kDefaultDisplayTime = 5.0f;

    bool TriggerDiscovery(std::uint32_t areaId, const std::string& name,
                          std::uint32_t xpReward, DiscoveryType type);

    [[nodiscard]] bool IsDiscovered(std::uint32_t areaId) const;

    [[nodiscard]] std::vector<std::uint32_t> GetDiscoveredAreas() const;
    [[nodiscard]] std::uint32_t GetDiscoveredCount() const;

    [[nodiscard]] std::optional<DiscoveryEvent> GetLastDiscovery() const;

    [[nodiscard]] std::uint32_t GetTotalXPFromDiscovery() const;

    [[nodiscard]] bool        HasPendingDiscovery() const;
    [[nodiscard]] std::string GetPendingDiscoveryName() const;
    [[nodiscard]] std::uint32_t GetPendingDiscoveryXP() const;
    void AcknowledgeDiscovery();

    [[nodiscard]] float GetDiscoveryDisplayTime() const;
    void SetDiscoveryDisplayTime(float seconds);

    void Update(float dt);

    void Reset();

 private:
    std::unordered_set<std::uint32_t>   discovered_;
    std::vector<DiscoveryEvent>         events_;
    std::uint32_t                       totalXp_         = 0;

    bool                                hasPending_      = false;
    std::string                         pendingName_;
    std::uint32_t                       pendingXp_       = 0;
    float                               pendingTimer_    = 0.0f;
    float                               displayTime_     = kDefaultDisplayTime;

    float                               clock_           = 0.0f;
};

}

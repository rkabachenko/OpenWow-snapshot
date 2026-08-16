
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class RaidTargetIconType : std::uint8_t {
    Star     = 0,
    Circle   = 1,
    Diamond  = 2,
    Triangle = 3,
    Moon     = 4,
    Square   = 5,
    Cross    = 6,
    Skull    = 7,
};

inline constexpr std::uint32_t kRaidTargetIconTypeCount = 8;

class RaidMarkerSystem {
public:

    void SetMarker(RaidTargetIconType icon, ObjectGuid unitGuid);

    void ClearMarker(RaidTargetIconType icon);

    void ClearAllMarkers();

    [[nodiscard]] ObjectGuid GetMarker(RaidTargetIconType icon) const;

    [[nodiscard]] std::optional<RaidTargetIconType> GetUnitMarker(ObjectGuid unitGuid) const;

    [[nodiscard]] bool HasMarker(ObjectGuid unitGuid) const;

    [[nodiscard]] std::vector<std::pair<RaidTargetIconType, ObjectGuid>> GetAllMarkers() const;

    [[nodiscard]] std::uint32_t GetActiveMarkerCount() const;

    [[nodiscard]] static std::string GetIconName(RaidTargetIconType icon);

    [[nodiscard]] static std::uint32_t GetIconIndex(RaidTargetIconType icon);

    [[nodiscard]] bool IsAvailable(RaidTargetIconType icon) const;

    [[nodiscard]] std::vector<RaidTargetIconType> GetAvailableIcons() const;

private:
    std::array<ObjectGuid, kRaidTargetIconTypeCount> slots_{};
};

}

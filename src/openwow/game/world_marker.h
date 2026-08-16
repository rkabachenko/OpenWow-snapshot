
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

inline constexpr std::uint32_t kMaxWorldMarkers = 5;

struct WorldMarkerEntry {
    std::uint32_t index    = 0;
    float         x        = 0.0f;
    float         y        = 0.0f;
    float         z        = 0.0f;
    std::uint32_t mapId    = 0;
    bool          isActive = false;
};

class WorldMarkerSystem {
public:

    void PlaceMarker(std::uint32_t index, float x, float y, float z,
                     std::uint32_t mapId);

    void ClearMarker(std::uint32_t index);

    void ClearAllMarkers();

    [[nodiscard]] std::optional<WorldMarkerEntry> GetMarker(std::uint32_t index) const;

    [[nodiscard]] bool IsMarkerActive(std::uint32_t index) const;

    [[nodiscard]] std::vector<WorldMarkerEntry> GetAllMarkers() const;

    [[nodiscard]] std::uint32_t GetActiveMarkerCount() const;

    [[nodiscard]] static std::uint32_t GetMarkerColor(std::uint32_t index);

    [[nodiscard]] static std::string GetMarkerName(std::uint32_t index);

    [[nodiscard]] static constexpr std::uint32_t GetMaxMarkers() { return kMaxWorldMarkers; }

    [[nodiscard]] float GetDistanceToMarker(std::uint32_t index, float playerX,
                                            float playerY, float playerZ) const;

private:
    std::array<WorldMarkerEntry, kMaxWorldMarkers> markers_{};
};

}

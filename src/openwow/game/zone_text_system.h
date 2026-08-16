
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace openwow::game {

enum class ZoneDisplayState : std::uint8_t {
    Hidden = 0,
    FadingIn,
    Visible,
    FadingOut,
};

enum class PvPStatus : std::uint8_t {
    Friendly = 0,
    Hostile,
    Contested,
    Sanctuary,
    FreeForAll,
    Combat,
};

class ZoneTextSystem {
 public:
    static ZoneTextSystem& Get();

    void SetCurrentZone(std::uint32_t zoneId, const std::string& zoneName,
                        PvPStatus status);
    void SetCurrentSubZone(std::uint32_t subZoneId, const std::string& subZoneName);

    [[nodiscard]] std::string GetCurrentZoneName() const;
    [[nodiscard]] std::string GetCurrentSubZoneName() const;
    [[nodiscard]] std::uint32_t GetCurrentZoneId() const;
    [[nodiscard]] std::uint32_t GetCurrentSubZoneId() const;

    [[nodiscard]] PvPStatus GetPvPStatus() const;

    [[nodiscard]] std::uint32_t GetPvPStatusColor() const;

    [[nodiscard]] ZoneDisplayState GetDisplayState() const;
    [[nodiscard]] float GetDisplayAlpha() const;
    [[nodiscard]] bool IsDisplayVisible() const;

    void SetInInstance(bool in_instance);
    [[nodiscard]] bool IsInInstance() const;

    void SetInstanceName(const std::string& name);
    [[nodiscard]] std::string GetInstanceName() const;

    void SetZoneLevel(const std::string& level);
    [[nodiscard]] std::string GetZoneLevel() const;

    void Update(float dt);

    void Reset();

    static constexpr float kFadeInDuration  = 0.5f;
    static constexpr float kVisibleDuration = 3.0f;
    static constexpr float kFadeOutDuration = 1.0f;

 private:
    ZoneTextSystem() = default;

    std::uint32_t zone_id_{0};
    std::string zone_name_;
    std::uint32_t subzone_id_{0};
    std::string subzone_name_;
    PvPStatus pvp_status_{PvPStatus::Friendly};

    bool in_instance_{false};
    std::string instance_name_;

    std::string zone_level_;

    ZoneDisplayState state_{ZoneDisplayState::Hidden};
    float timer_{0.0f};

    mutable std::mutex mutex_;
};

}

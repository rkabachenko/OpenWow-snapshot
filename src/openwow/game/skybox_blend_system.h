#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "openwow/game/time_of_day_windows.h"

namespace openwow::game {

enum class SkyboxColorBand : uint8_t {
    Top              = 0,
    Middle           = 1,
    MiddleToHorizon  = 2,
    AboveHorizon     = 3,
    Horizon          = 4,
    Fog              = 5,
    Sun              = 6,
    SunHalo          = 7,
    CloudDark        = 8,
    CloudLight       = 9,
    Count            = 10,
};

struct SkyColor3 {
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

struct SkyboxTimeSlice {
    float timeOfDay = 0.0f;
    std::array<SkyColor3, static_cast<size_t>(SkyboxColorBand::Count)> colors{};
};

struct SkyboxBlendState {
    float currentTime = 0.0f;
    std::array<SkyColor3, static_cast<size_t>(SkyboxColorBand::Count)>
        interpolatedColors{};
    float fogDistance   = 500.0f;
    float fogMultiplier = 1.0f;
    SkyColor3 celestialGlowColor;
};

class SkyboxBlendSystem {
public:
    static constexpr uint32_t kBandCount        = static_cast<uint32_t>(SkyboxColorBand::Count);
    static constexpr float    kNightStart        = kObservedDayEndHour;
    static constexpr float    kNightEnd          = kObservedDayStartHour;
    static constexpr float    kDawnStart         = 5.0f;
    static constexpr float    kDawnEnd           = 7.0f;
    static constexpr float    kDuskStart         = 19.0f;
    static constexpr float    kDuskEnd           = 21.0f;

    SkyboxBlendSystem() = default;

    void SetLightId(uint32_t lightId);
    [[nodiscard]] uint32_t GetLightId() const { return lightId_; }

    void AddTimeSlice(SkyboxTimeSlice slice);
    [[nodiscard]] uint32_t GetTimeSliceCount() const;

    void  SetCurrentTime(float hourOfDay);
    [[nodiscard]] float GetCurrentTime() const { return currentTime_; }

    [[nodiscard]] SkyboxBlendState GetBlendState() const;
    [[nodiscard]] SkyColor3        GetBandColor(SkyboxColorBand band) const;
    [[nodiscard]] SkyColor3        GetFogColor() const;

    [[nodiscard]] float GetFogDistance() const { return fogDistance_; }
    void SetFogDistance(float dist) { fogDistance_ = dist; }
    void SetFogMultiplier(float mult) { fogMultiplier_ = mult; }

    [[nodiscard]] bool IsNightTime() const;
    [[nodiscard]] bool IsDawnDusk() const;

private:

    void SortSlices();

    [[nodiscard]] static SkyColor3 Lerp(const SkyColor3& a, const SkyColor3& b,
                                         float t);

    [[nodiscard]] std::array<SkyColor3, kBandCount> InterpolateColors() const;

    uint32_t                     lightId_       = 0;
    float                        currentTime_   = 12.0f;
    float                        fogDistance_    = 500.0f;
    float                        fogMultiplier_ = 1.0f;
    std::vector<SkyboxTimeSlice> slices_;
};

}

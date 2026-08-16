#include "openwow/game/skybox_blend_system.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void SkyboxBlendSystem::SetLightId(uint32_t lightId) { lightId_ = lightId; }

void SkyboxBlendSystem::AddTimeSlice(SkyboxTimeSlice slice) {

    slice.timeOfDay = std::fmod(slice.timeOfDay, 24.0f);
    if (slice.timeOfDay < 0.0f) slice.timeOfDay += 24.0f;
    slices_.push_back(std::move(slice));
    SortSlices();
}

uint32_t SkyboxBlendSystem::GetTimeSliceCount() const {
    return static_cast<uint32_t>(slices_.size());
}

void SkyboxBlendSystem::SetCurrentTime(float hourOfDay) {
    currentTime_ = std::fmod(hourOfDay, 24.0f);
    if (currentTime_ < 0.0f) currentTime_ += 24.0f;
}

SkyboxBlendState SkyboxBlendSystem::GetBlendState() const {
    SkyboxBlendState state;
    state.currentTime      = currentTime_;
    state.fogDistance       = fogDistance_;
    state.fogMultiplier    = fogMultiplier_;
    state.interpolatedColors = InterpolateColors();

    auto sun  = state.interpolatedColors[static_cast<size_t>(SkyboxColorBand::Sun)];
    auto halo = state.interpolatedColors[static_cast<size_t>(SkyboxColorBand::SunHalo)];
    state.celestialGlowColor = {
        (sun.r + halo.r) * 0.5f,
        (sun.g + halo.g) * 0.5f,
        (sun.b + halo.b) * 0.5f,
    };
    return state;
}

SkyColor3 SkyboxBlendSystem::GetBandColor(SkyboxColorBand band) const {
    auto colors = InterpolateColors();
    auto idx = static_cast<size_t>(band);
    if (idx >= kBandCount) return {};
    return colors[idx];
}

SkyColor3 SkyboxBlendSystem::GetFogColor() const {
    return GetBandColor(SkyboxColorBand::Fog);
}

bool SkyboxBlendSystem::IsNightTime() const {
    return IsObservedNighttime(currentTime_);
}

bool SkyboxBlendSystem::IsDawnDusk() const {
    return (currentTime_ >= kDawnStart && currentTime_ < kDawnEnd) ||
           (currentTime_ >= kDuskStart && currentTime_ < kDuskEnd);
}

void SkyboxBlendSystem::SortSlices() {
    std::sort(slices_.begin(), slices_.end(),
              [](const SkyboxTimeSlice& a, const SkyboxTimeSlice& b) {
                  return a.timeOfDay < b.timeOfDay;
              });
}

SkyColor3 SkyboxBlendSystem::Lerp(const SkyColor3& a, const SkyColor3& b,
                                   float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
    };
}

std::array<SkyColor3, SkyboxBlendSystem::kBandCount>
SkyboxBlendSystem::InterpolateColors() const {
    std::array<SkyColor3, kBandCount> result{};

    if (slices_.empty()) return result;
    if (slices_.size() == 1) return slices_[0].colors;

    const auto n = slices_.size();

    size_t upper = 0;
    for (size_t i = 0; i < n; ++i) {
        if (slices_[i].timeOfDay > currentTime_) {
            upper = i;
            break;
        }
        if (i == n - 1) upper = 0;
    }
    size_t lower = (upper == 0) ? n - 1 : upper - 1;

    float tLow  = slices_[lower].timeOfDay;
    float tHigh = slices_[upper].timeOfDay;

    float span;
    float pos;
    if (tHigh <= tLow) {

        span = (24.0f - tLow) + tHigh;
        pos  = (currentTime_ >= tLow) ? (currentTime_ - tLow)
                                       : (24.0f - tLow + currentTime_);
    } else {
        span = tHigh - tLow;
        pos  = currentTime_ - tLow;
    }

    float t = (span > 0.0f) ? std::clamp(pos / span, 0.0f, 1.0f) : 0.0f;

    for (uint32_t i = 0; i < kBandCount; ++i) {
        result[i] = Lerp(slices_[lower].colors[i], slices_[upper].colors[i], t);
    }
    return result;
}

}

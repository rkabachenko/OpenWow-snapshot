#include "openwow/render/resources/shaders/sky_shader_data.h"

#include <algorithm>
#include <cmath>

namespace openwow::render {

SkyShaderData::SkyShaderData() {

  SetBandColor(SkyBandType::Top, 0.1f, 0.15f, 0.5f, 1.0f);
  SetBandColor(SkyBandType::Middle, 0.3f, 0.45f, 0.8f, 1.0f);
  SetBandColor(SkyBandType::Horizon, 0.6f, 0.7f, 0.9f, 1.0f);
  SetBandColor(SkyBandType::Fog, 0.7f, 0.75f, 0.85f, 1.0f);
  SetBandColor(SkyBandType::Sun, 1.0f, 0.95f, 0.7f, 1.0f);
  SetBandColor(SkyBandType::Stars, 1.0f, 1.0f, 1.0f, 0.0f);
  SetBandColor(SkyBandType::CloudLayer1, 0.9f, 0.9f, 0.95f, 0.5f);
  SetBandColor(SkyBandType::CloudLayer2, 0.85f, 0.85f, 0.9f, 0.3f);
}

SkyShaderData::~SkyShaderData() = default;

void SkyShaderData::SetBandColor(SkyBandType band, float r, float g, float b,
                                  float a) {
  bands_[static_cast<uint8_t>(band)] =
      SkyBandColor{band,
                   std::clamp(r, 0.0f, 1.0f),
                   std::clamp(g, 0.0f, 1.0f),
                   std::clamp(b, 0.0f, 1.0f),
                   std::clamp(a, 0.0f, 1.0f)};
}

SkyBandColor SkyShaderData::GetBandColor(SkyBandType band) const {
  auto it = bands_.find(static_cast<uint8_t>(band));
  if (it != bands_.end()) return it->second;
  return SkyBandColor{band, 0.0f, 0.0f, 0.0f, 1.0f};
}

void SkyShaderData::SetAllBands(const std::vector<SkyBandColor>& bands) {
  for (const auto& bc : bands) {
    bands_[static_cast<uint8_t>(bc.band)] = bc;
  }
}

std::vector<SkyBandColor> SkyShaderData::GetAllBands() const {
  std::vector<SkyBandColor> result;
  result.reserve(bands_.size());
  for (const auto& [key, val] : bands_) {
    result.push_back(val);
  }
  return result;
}

void SkyShaderData::SetSunPosition(float azimuth, float elevation) {
  sunAzimuth_ = azimuth;
  sunElevation_ = elevation;
}

SkySunDirection SkyShaderData::GetSunDirection() const {

  const float ce = std::cos(sunElevation_);
  return SkySunDirection{
      ce * std::sin(sunAzimuth_),
      std::sin(sunElevation_),
      ce * std::cos(sunAzimuth_)};
}

void SkyShaderData::SetCloudDensity(float density) {
  cloudDensity_ = std::clamp(density, 0.0f, 1.0f);
}

float SkyShaderData::GetCloudDensity() const { return cloudDensity_; }

void SkyShaderData::SetCloudSpeed(float speed) { cloudSpeed_ = speed; }
float SkyShaderData::GetCloudSpeed() const { return cloudSpeed_; }

void SkyShaderData::SetStarIntensity(float intensity) {
  starIntensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

float SkyShaderData::GetStarIntensity() const { return starIntensity_; }

SkyColorRGB SkyShaderData::GetSkyColorAtAngle(float angle) const {

  constexpr float kHalfPi = 1.5707963268f;
  const float t = std::clamp(angle / kHalfPi, 0.0f, 1.0f);

  auto lerpC = [](const SkyBandColor& a, const SkyBandColor& b,
                  float f) -> SkyColorRGB {
    return SkyColorRGB{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f,
                       a.b + (b.b - a.b) * f};
  };

  const auto fog = GetBandColor(SkyBandType::Fog);
  const auto horizon = GetBandColor(SkyBandType::Horizon);
  const auto middle = GetBandColor(SkyBandType::Middle);
  const auto top = GetBandColor(SkyBandType::Top);

  if (t < 0.25f) {
    return lerpC(fog, horizon, t / 0.25f);
  } else if (t < 0.6f) {
    return lerpC(horizon, middle, (t - 0.25f) / 0.35f);
  } else {
    return lerpC(middle, top, (t - 0.6f) / 0.4f);
  }
}

void SkyShaderData::SetNightAlpha(float alpha) {
  nightAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
}

float SkyShaderData::GetNightAlpha() const { return nightAlpha_; }

void SkyShaderData::Reset() {
  bands_.clear();
  sunAzimuth_ = 0.0f;
  sunElevation_ = 0.7854f;
  cloudDensity_ = 0.3f;
  cloudSpeed_ = 1.0f;
  starIntensity_ = 0.0f;
  nightAlpha_ = 0.0f;

  SetBandColor(SkyBandType::Top, 0.1f, 0.15f, 0.5f, 1.0f);
  SetBandColor(SkyBandType::Middle, 0.3f, 0.45f, 0.8f, 1.0f);
  SetBandColor(SkyBandType::Horizon, 0.6f, 0.7f, 0.9f, 1.0f);
  SetBandColor(SkyBandType::Fog, 0.7f, 0.75f, 0.85f, 1.0f);
  SetBandColor(SkyBandType::Sun, 1.0f, 0.95f, 0.7f, 1.0f);
  SetBandColor(SkyBandType::Stars, 1.0f, 1.0f, 1.0f, 0.0f);
  SetBandColor(SkyBandType::CloudLayer1, 0.9f, 0.9f, 0.95f, 0.5f);
  SetBandColor(SkyBandType::CloudLayer2, 0.85f, 0.85f, 0.9f, 0.3f);
}

}

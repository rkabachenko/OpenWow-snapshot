#pragma once

#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace openwow::render {

enum class SkyBandType : uint8_t {
  Top = 0,
  Middle = 1,
  Horizon = 2,
  Fog = 3,
  Sun = 4,
  Stars = 5,
  CloudLayer1 = 6,
  CloudLayer2 = 7
};

struct SkyBandColor {
  SkyBandType band = SkyBandType::Top;
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

struct SkySunDirection {
  float x = 0.0f;
  float y = 1.0f;
  float z = 0.0f;
};

struct SkyColorRGB {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

class SkyShaderData {
 public:
  SkyShaderData();
  ~SkyShaderData();

  void SetBandColor(SkyBandType band, float r, float g, float b, float a);
  [[nodiscard]] SkyBandColor GetBandColor(SkyBandType band) const;

  void SetAllBands(const std::vector<SkyBandColor>& bands);
  [[nodiscard]] std::vector<SkyBandColor> GetAllBands() const;

  void SetSunPosition(float azimuth, float elevation);
  [[nodiscard]] SkySunDirection GetSunDirection() const;

  void SetCloudDensity(float density);
  [[nodiscard]] float GetCloudDensity() const;

  void SetCloudSpeed(float speed);
  [[nodiscard]] float GetCloudSpeed() const;

  void SetStarIntensity(float intensity);
  [[nodiscard]] float GetStarIntensity() const;

  [[nodiscard]] SkyColorRGB GetSkyColorAtAngle(float angle) const;

  void SetNightAlpha(float alpha);
  [[nodiscard]] float GetNightAlpha() const;

  void Reset();

 private:
  std::unordered_map<uint8_t, SkyBandColor> bands_;
  float sunAzimuth_ = 0.0f;
  float sunElevation_ = 0.7854f;
  float cloudDensity_ = 0.3f;
  float cloudSpeed_ = 1.0f;
  float starIntensity_ = 0.0f;
  float nightAlpha_ = 0.0f;
};

}

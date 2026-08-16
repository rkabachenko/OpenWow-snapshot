#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::game {

enum class FogVolumeShape : uint8_t {
    Sphere   = 0,
    Box      = 1,
    Cylinder = 2,
};

enum class FogBlendMode : uint8_t {
    Replace  = 0,
    Additive = 1,
    Multiply = 2,
};

struct FogVolumeInfo {
    uint32_t       volumeId     = 0;
    FogVolumeShape shape        = FogVolumeShape::Sphere;
    struct { float x = 0.0f, y = 0.0f, z = 0.0f; } position;
    float          radius       = 10.0f;
    struct { float x = 10.0f, y = 10.0f, z = 10.0f; } extents;
    float          height       = 10.0f;
    float          orientation  = 0.0f;
    struct { float r = 0.5f, g = 0.5f, b = 0.5f; } color;
    float          density      = 0.5f;
    float          startDistance = 0.0f;
    float          endDistance   = 50.0f;
    FogBlendMode   blendMode    = FogBlendMode::Replace;
    uint32_t       priority     = 0;
    float          innerRadius  = 5.0f;
};

struct FogVolumeResult {
    bool  inFog      = false;
    struct { float r = 0.0f, g = 0.0f, b = 0.0f; } fogColor;
    float fogDensity = 0.0f;
    float fogStart   = 0.0f;
    float fogEnd     = 0.0f;
};

class FogVolumeSystem {
public:
    static constexpr uint32_t kDefaultMaxVolumes = 32;

    FogVolumeSystem() = default;

    uint32_t AddVolume(FogVolumeInfo info);
    void     RemoveVolume(uint32_t volumeId);
    [[nodiscard]] std::optional<FogVolumeInfo> GetVolume(uint32_t volumeId) const;

    [[nodiscard]] bool  IsPointInVolume(uint32_t volumeId,
                                         float x, float y, float z) const;

    [[nodiscard]] float GetVolumeBlendFactor(uint32_t volumeId,
                                              float x, float y, float z) const;

    [[nodiscard]] FogVolumeResult QueryFogAtPoint(float x, float y,
                                                   float z) const;

    [[nodiscard]] std::vector<FogVolumeInfo> GetActiveVolumes() const;

    [[nodiscard]] std::vector<FogVolumeInfo> GetVolumesAtPoint(
        float x, float y, float z) const;

    [[nodiscard]] uint32_t GetActiveCount() const;
    void ClearAll();
    void SetMaxVolumes(uint32_t max);

private:

    [[nodiscard]] static float DistanceToVolume(const FogVolumeInfo& vol,
                                                 float x, float y, float z);

    [[nodiscard]] static bool PointInsideVolume(const FogVolumeInfo& vol,
                                                 float x, float y, float z);

    [[nodiscard]] static float ComputeBlendFactor(const FogVolumeInfo& vol,
                                                   float x, float y, float z);

    uint32_t nextId_    = 1;
    uint32_t maxVolumes_ = kDefaultMaxVolumes;
    std::unordered_map<uint32_t, FogVolumeInfo> volumes_;
};

}

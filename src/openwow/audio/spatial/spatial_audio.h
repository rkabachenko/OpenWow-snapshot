#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::audio {

struct Vec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    [[nodiscard]] float DistanceTo(const Vec3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    [[nodiscard]] float DistanceToXY(const Vec3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    [[nodiscard]] float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    [[nodiscard]] Vec3 Normalized() const {
        float len = Length();
        if (len < 1e-6f) return {0.0f, 0.0f, 0.0f};
        return {x / len, y / len, z / len};
    }
};

struct ListenerState {
    Vec3  position;
    float facing{0.0f};
    Vec3  forward{0.0f, 1.0f, 0.0f};
    Vec3  right{1.0f, 0.0f, 0.0f};

    void UpdateFromFacing() {
        forward = {std::cos(facing), std::sin(facing), 0.0f};

        right = {std::sin(facing), -std::cos(facing), 0.0f};
    }
};

struct SpatialSource {
    std::uint32_t sourceId{0};
    std::uint32_t soundKitId{0};
    Vec3          position;
    float         minDistance{0.0f};
    float         maxDistance{40.0f};
    float         baseVolume{1.0f};
    std::uint32_t priority{0};
    bool          active{true};

    float computedVolume{0.0f};
    float computedPan{0.0f};
    float distanceToListener{0.0f};
};

struct SpatialResult {
    float volume{0.0f};
    float pan{0.0f};
    float distance{0.0f};
    bool  culled{false};
};

enum class SpeakerMode : std::uint32_t {
    kMono    = 1,
    kStereo  = 2,
    kQuad    = 4,
    kSurround = 6,
    k7point1 = 8,
};

[[nodiscard]] SpeakerMode DetectSpeakerMode();

[[nodiscard]] const char* SpeakerModeToString(SpeakerMode mode);

class SpatialAudio {
public:
    SpatialAudio() = default;
    ~SpatialAudio() = default;

    void SetListener(const ListenerState& listener);
    void SetListenerPosition(float x, float y, float z, float facing);
    [[nodiscard]] const ListenerState& GetListener() const { return listener_; }

    std::uint32_t AddSource(const SpatialSource& source);

    void RemoveSource(std::uint32_t sourceId);

    void UpdateSourcePosition(std::uint32_t sourceId, float x, float y, float z);

    [[nodiscard]] std::optional<SpatialSource> GetSource(std::uint32_t sourceId) const;

    [[nodiscard]] std::vector<SpatialSource> GetActiveSources() const;

    [[nodiscard]] std::uint32_t GetActiveSourceCount() const;

    [[nodiscard]] static SpatialResult ComputeSpatial(
        const ListenerState& listener,
        const Vec3& sourcePos,
        float minDistance,
        float maxDistance);

    [[nodiscard]] static float ComputeDistanceAttenuation(
        float distance, float minDistance, float maxDistance);

    [[nodiscard]] static float ComputePan(
        const ListenerState& listener, const Vec3& sourcePos);

    void Update();

    void SetMaxAudibleDistance(float dist) { maxAudibleDistance_ = dist; }
    [[nodiscard]] float GetMaxAudibleDistance() const { return maxAudibleDistance_; }

    void SetRolloffFactor(float factor) { rolloffFactor_ = factor; }
    [[nodiscard]] float GetRolloffFactor() const { return rolloffFactor_; }

    void SetDopplerFactor(float factor) { dopplerFactor_ = factor; }
    [[nodiscard]] float GetDopplerFactor() const { return dopplerFactor_; }

    void Reset();

    SpeakerMode DetectAndCacheSpeakerMode();

    [[nodiscard]] SpeakerMode GetSpeakerMode() const { return speaker_mode_; }

    [[nodiscard]] int GetSpeakerChannelCount() const;

private:
    ListenerState listener_;
    std::unordered_map<std::uint32_t, SpatialSource> sources_;
    std::uint32_t nextSourceId_{1};

    float maxAudibleDistance_{100.0f};
    float rolloffFactor_{1.0f};
    float dopplerFactor_{0.0f};

    SpeakerMode speaker_mode_{SpeakerMode::kStereo};
};

}
